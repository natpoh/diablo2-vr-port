// skeletons.cpp - SkeletonInstance::ComputeSelfWorldPose, hooked.
//
// Found 2026-10-02 in the D2RLoader-layout snapshot (docs/arms_recon.md):
//   ComputeSelfWorldPose(SkeletonInstance* this, float, float, bool) @ RVA 0xF78740
//   this+0x08  granny_model_instance*   -> +0x08 granny_skeleton* (GrannyGetSourceSkeleton)
//   this+0x18  granny_local_pose*
//   this+0x20  granny_world_pose*        (m_grannyWorldPose, built with Offset = NULL:
//   this+0x28  granny_world_pose*         model space, not world)  (previous frame)
//   granny_world_pose: +0 int32 BoneCount, +4 float (*)[16] matrices (Granny packs to 4)
//   granny_skeleton:   +0 char* Name, +8 int32 BoneCount, +0xC granny_bone* Bones
//   granny_bone:       +0 char* Name, +8 int32 ParentIndex, stride 0xA4
//
// After the game poses the hero, this turns the body to where the camera looks,
// drives the arms (and the hands' turn) from the controllers and hides the head.
// On F10 (Dump) every distinct skeleton updated in the next second goes to
// d2r_vr_skeletons.txt with its bones, parents and model-space translations.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <share.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "bow_aim.h"
#include "bow_census.h"
#include "bow_hook.h"
#include "bow_hand.h"
#include "bow_rig.h"
#include "granny_mesh.h"   // [bow] arrow_fx: the arrow mesh's box off the model in memory
#include "guarded_text.h"  // Str: a name read a page at a time
#include "bone_guard.h"
#include "bow_touch.h"
#include "flail_chain.h"   // [flail] chain: the flail's head swings free on its chain
#include "head_hang.h"     // [shield] heads_hang: the Necromancer's head hangs from its cord
#include "head_shadow.h"   // [arms] head_shadow: the head shrunk in the eyes' skinning only, its shadow cast
#include "grip_fit.h"
#include "grip_pose.h"
#include "weapon_extents.h"   // melee by touch: the weapon model's unit in metres as drawn (PosedBlade::unitM)
#include "grip_rig.h"
#include "grab_bodies.h"   // [weight] grab_wrap: the palm laid on a held body without the grip library (LayPalm)
#include "skeletons.h"
#include "gait_lib.h"   // [multiplayer] pose_share_legs: the gait library (d2r_vr_gait.dll) the other heroes' legs step by
#include "leg_chain.h"  // the legs by the rig's hierarchy where its bones' names say nothing (the barbarian's)
#include "leg_ik.h"     // the gait's knee and leg bones' turns from the rest pose and the bend plane (never a knee bent the wrong way)
#include "walk_back.h"  // [body] walk_back_reverse: the game's own walk played backwards while he walks back
#include "shield_arm.h"
#include "shield_arm_wire.h"
#include "ranged_hand_wire.h"
#include "shield_fist.h"
#include "forearm_twist.h"
#include "full_reach.h"      // [arms] full_reach: the hand always reaches its controller (F4)
#include "view_gate.h"
#include "frame_trace.h"   // [debug] frame_trace: what the pose hook costs a pair
#include "body_calibration.h"
#include "crouch.h"         // [body] crouch: the hero crouches when you do (F4)
#include "trunk_hold.h"     // [body] lock (F4) and flat_hold_trunk (flat F3): which pose each bone above the pelvis takes
#include "sigscan.h"
#include "weapon_back.h"
#include "weapon_shaft.h"
#include "weight.h"
#include "monster_humanoid.h"   // [weight] monster_ragdoll: the doll on humanoids only
#include "bone_segments.h"   // [weight] monster_ragdoll_all: bodies from bone segments
#include "corpse_watch.h"    // [weight] monster_ragdoll: a corpse is no doll
#include "xbow_grip.h"   // [hands] xbow_grip_latch / xbow_grip_wide / xbow_support_model: the crossbow's left hand

namespace skel {

namespace {

#pragma optimize("", off)
bool SafeCopy(void* dst, const void* src, size_t n) noexcept {
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma optimize("", on)

template <class T> bool Get(uintptr_t a, T* v) { return a > 0x10000 && SafeCopy(v, (const void*)a, sizeof(T)); }
bool SafeWrite(void* dst, const void* src, size_t n) { return (uintptr_t)dst > 0x10000 && SafeCopy(dst, src, n); }

// A name in game memory: one guarded copy per page, not one per letter (vr/guarded_text.h, bearstonem/d2r-pcvr's); the same answers. Printable ASCII, up to 95 letters.
std::string Str(uintptr_t p) {
    char buf[96];
    const int n = guardedtext::ReadText(p, buf, (int)sizeof buf, SafeCopy);
    return n < 0 ? std::string() : std::string(buf, n);
}
// A SkeletonInstance's model path (its name object's +8).
std::string ModelOf(uintptr_t self) {
    uintptr_t object = 0, path = 0;
    Get(self, &object); Get(object + 8, &path);
    return Str(path);
}

// A rig cache (the bow's, the flail's, the crossbow's: every skeleton of the size is met) asked under the SHARED lock first: a rig known by the same bones that is not one
// of what the cache is for leaves at once - most skeletons the game poses - without the exclusive lock its own rigs are posed under, which held up the other render workers.
template <class Map, class Key, class Not>
bool KnownNot(SRWLOCK& lock, const Map& map, const Key& key, uintptr_t bones, Not&& isNot) {
    AcquireSRWLockShared(&lock);
    const auto it = map.find(key);
    const bool known = it != map.end() && it->second.bones == bones && isNot(it->second);
    ReleaseSRWLockShared(&lock);
    return known;
}

SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<ULONGLONG> g_dumpUntil{0};
std::set<std::string> g_seen;
FILE* g_out = nullptr;
std::atomic<uint64_t> g_calls{0};

// The world camera's look-at, from vrcam's timer, and when it came: read by the pose hook on
// the game's threads, so under its lock (it was written bare, 2026-10-13).
// Still: the look-at has stayed within half a unit of one place since stillSince - its spring
// has caught up with the hero, who stands there.
SRWLOCK g_heroPosLock = SRWLOCK_INIT;
float g_heroPos[3] = {};
ULONGLONG g_heroPosAt = 0;
float g_stillAt[2] = {};
ULONGLONG g_stillSince = 0;

bool HeroPos(float out[3], ULONGLONG* at = nullptr, ULONGLONG* stillSince = nullptr) {
    AcquireSRWLockShared(&g_heroPosLock);
    memcpy(out, g_heroPos, sizeof g_heroPos);
    const ULONGLONG t = g_heroPosAt;
    if (stillSince) *stillSince = g_stillSince;
    ReleaseSRWLockShared(&g_heroPosLock);
    if (at) *at = t;
    return t != 0;
}

// The hero's world placement is applied at draw time, not in the pose. Look for
// it near the SkeletonInstance: in it, in what it points at, and one more hop -
// any spot whose floats read as the hero's x and z, with the floats around it.
void FindHeroTransform(uintptr_t self) {
    float heroPos[3];
    HeroPos(heroPos);
    fprintf(g_out, "   looking for the hero position %.2f %.2f %.2f near the instance\n", heroPos[0], heroPos[1], heroPos[2]);
    std::set<uintptr_t> scanned;
    int hits = 0;
    auto scan = [&](uintptr_t base, const char* how, uintptr_t via) {
        if (base < 0x10000 || !scanned.insert(base).second || hits > 40) return;
        float f[0x600 / 4];
        if (!SafeCopy(f, (const void*)base, sizeof f)) return;
        for (int i = 0; i + 2 < (int)(sizeof f / 4) && hits <= 40; ++i) {
            if (fabsf(f[i] - heroPos[0]) > 3.0f) continue;
            const int zi = fabsf(f[i + 1] - heroPos[2]) < 3.0f ? i + 1 : fabsf(f[i + 2] - heroPos[2]) < 3.0f ? i + 2 : -1;
            if (zi < 0) continue;
            ++hits;
            fprintf(g_out, "   HIT %s %p +0x%X (via %p): x %.3f, z at +%d\n", how, (void*)base, i * 4, (void*)via, f[i], (zi - i) * 4);
            const int s = std::max(0, i - 14);
            for (int k = s; k < s + 20 && k < (int)(sizeof f / 4); k += 4)
                fprintf(g_out, "      +0x%03X: %10.4f %10.4f %10.4f %10.4f\n", k * 4, f[k], f[k + 1], f[k + 2], f[k + 3]);
        }
    };
    scan(self, "self", 0);
    uint64_t q1[0x400 / 8];
    if (!SafeCopy(q1, (const void*)self, sizeof q1)) return;
    for (int a = 0; a < (int)(sizeof q1 / 8); ++a) {
        if (q1[a] < 0x10000 || q1[a] > 0x7FFFFFFFFFFFull) continue;
        scan((uintptr_t)q1[a], "self->", self + a * 8);
        uint64_t q2[0x200 / 8];
        if (!SafeCopy(q2, (const void*)q1[a], sizeof q2)) continue;
        for (int b = 0; b < (int)(sizeof q2 / 8); ++b)
            if (q2[b] > 0x10000 && q2[b] < 0x7FFFFFFFFFFFull) scan((uintptr_t)q2[b], "self->->", (uintptr_t)q1[a] + b * 8);
    }
    fprintf(g_out, "   %d hits\n", hits);
}

void DumpOne(uintptr_t self) {
    uintptr_t modelInst = 0, skel = 0, world = 0, nameObj = 0;
    if (!Get(self + 0x08, &modelInst) || !Get(modelInst + 0x08, &skel) || !Get(self + 0x20, &world)) return;
    Get(self, &nameObj);
    uintptr_t nameP = 0; Get(nameObj + 8, &nameP);
    const std::string model = Str(nameP);
    uintptr_t skelNameP = 0, bones = 0; int32_t count = 0;
    Get(skel, &skelNameP); Get(skel + 8, &count); Get(skel + 0xC, &bones);
    const std::string skelName = Str(skelNameP);
    const std::string key = skelName + "|" + std::to_string(count);
    if (count <= 0 || count > 1024 || !g_seen.insert(key).second || !g_out) return;

    int32_t wpCount = 0; uintptr_t mats = 0;
    Get(world, &wpCount); Get(world + 4, &mats);
    fprintf(g_out, "\n== instance %p  model '%s'  skeleton '%s' @%p  bones %d @%p  world pose %p (%d bones, matrices %p)\n",
            (void*)self, model.c_str(), skelName.c_str(), (void*)skel, count, (void*)bones, (void*)world, wpCount, (void*)mats);
    // stride: 0xA4 expected; confirm with the hierarchy, else try the neighbours
    size_t stride = 0;
    for (size_t s : {0xA4, 0xA8, 0xA0, 0xAC, 0xB0, 0x9C, 0xB4, 0xB8, 0xC0}) {
        bool ok = true;
        for (int i = 0; i < count && ok; ++i) {
            int32_t parent = -2; uintptr_t nm = 0;
            ok = Get(bones + i * s + 8, &parent) && Get(bones + i * s, &nm) && !Str(nm).empty() &&
                 (i == 0 ? parent == -1 : (parent >= -1 && parent < i));
        }
        if (ok) { stride = s; break; }
    }
    fprintf(g_out, "   bone stride 0x%zX%s\n", stride, stride ? "" : " (NOT FOUND - raw first bytes below)");
    if (!stride) {
        uint8_t raw[0x180] = {};
        if (SafeCopy(raw, (const void*)bones, sizeof raw))
            for (int i = 0; i < (int)sizeof raw; i += 16) {
                fprintf(g_out, "   +%03X:", i);
                for (int k = 0; k < 16; k += 4) { uint32_t v; memcpy(&v, raw + i + k, 4); fprintf(g_out, " %08X", v); }
                fprintf(g_out, "\n");
            }
        return;
    }
    if (model.find("/character/player/") != std::string::npos) FindHeroTransform(self);
    for (int i = 0; i < count; ++i) {
        uintptr_t nm = 0; int32_t parent = 0; float m[16] = {};
        Get(bones + i * stride, &nm); Get(bones + i * stride + 8, &parent);
        const bool haveM = i < wpCount && SafeCopy(m, (const void*)(mats + i * 64), 64);
        fprintf(g_out, "   [%3d] parent %3d  %-40s", i, parent, Str(nm).c_str());
        if (haveM) fprintf(g_out, "  t %8.3f %8.3f %8.3f  x %6.3f %6.3f %6.3f", m[12], m[13], m[14], m[0], m[1], m[2]);
        fprintf(g_out, "\n");
    }
    fflush(g_out);
}

// ---------------------------------------------------------------------------
// Arms. The world pose is in model space (y up, the model faces +z, its left
// is +x), one row-major 4x4 per bone, row-vector convention: W = Local * W(parent),
// translation in [12..14]. Bones come parents first, so one pass in index order
// re-derives every descendant of a bone we turned.

struct Mat { float m[16]; };

// a * b: each row of a, as a row vector, taken through b.
Mat Mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k) {
            const float x = a.m[row * 4 + k];
            for (int col = 0; col < 4; ++col) r.m[row * 4 + col] += x * b.m[k * 4 + col];
        }
    return r;
}

// Inverse of an affine row-vector matrix (rotation+scale part by full 3x3 inverse).
Mat InvAffine(const Mat& a) {
    const float* m = a.m;
    const float c00 = m[5]*m[10] - m[6]*m[9], c01 = m[6]*m[8] - m[4]*m[10], c02 = m[4]*m[9] - m[5]*m[8];
    const float det = m[0]*c00 + m[1]*c01 + m[2]*c02;
    const float id = fabsf(det) > 1e-12f ? 1.0f / det : 0.0f;
    Mat r{};
    r.m[0] = c00*id;                       r.m[1] = (m[2]*m[9] - m[1]*m[10])*id; r.m[2] = (m[1]*m[6] - m[2]*m[5])*id;
    r.m[4] = c01*id;                       r.m[5] = (m[0]*m[10] - m[2]*m[8])*id; r.m[6] = (m[2]*m[4] - m[0]*m[6])*id;
    r.m[8] = c02*id;                       r.m[9] = (m[1]*m[8] - m[0]*m[9])*id;  r.m[10] = (m[0]*m[5] - m[1]*m[4])*id;
    for (int j = 0; j < 3; ++j) r.m[12+j] = -(m[12]*r.m[j] + m[13]*r.m[4+j] + m[14]*r.m[8+j]);
    r.m[15] = 1.0f;
    return r;
}

struct V3 { float x, y, z; };
V3 Pos(const Mat& a) { return {a.m[12], a.m[13], a.m[14]}; }
V3 Sub(V3 a, V3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
float Dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
V3 Cross(V3 a, V3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
float Len(V3 a) { return sqrtf(Dot(a, a)); }
V3 Norm(V3 a) { const float l = Len(a); return l > 1e-6f ? V3{a.x/l, a.y/l, a.z/l} : V3{0, 0, 1}; }

// The row-vector rotation (v' = v * R, as a 4x4 with no translation) taking direction a onto b.
Mat RotBetween(V3 a, V3 b) {
    a = Norm(a); b = Norm(b);
    const V3 axis = Cross(a, b);
    const float s = Len(axis), c = Dot(a, b);
    Mat r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    if (s < 1e-6f) {
        if (c > 0) return r;
        // opposite: half turn about any axis perpendicular to a
        V3 p = fabsf(a.x) < 0.9f ? Norm(Cross(a, {1, 0, 0})) : Norm(Cross(a, {0, 1, 0}));
        const float x = p.x, y = p.y, z = p.z;
        const float R[9] = {2*x*x-1, 2*x*y, 2*x*z, 2*x*y, 2*y*y-1, 2*y*z, 2*x*z, 2*y*z, 2*z*z-1};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i*4+j] = R[i*3+j];
        return r;
    }
    const V3 k = {axis.x/s, axis.y/s, axis.z/s};
    const float t = 1 - c;
    // column-convention Rodrigues, transposed for row vectors
    const float Rc[9] = {c + k.x*k.x*t,     k.x*k.y*t - k.z*s, k.x*k.z*t + k.y*s,
                         k.y*k.x*t + k.z*s, c + k.y*k.y*t,     k.y*k.z*t - k.x*s,
                         k.z*k.x*t - k.y*s, k.z*k.y*t + k.x*s, c + k.z*k.z*t};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i*4+j] = Rc[j*3+i];
    return r;
}

// Turn bone b about its own position so its rotation part is followed by R.
void TurnAboutSelf(Mat& w, const Mat& R) {
    const V3 p = Pos(w);
    w.m[12] = w.m[13] = w.m[14] = 0;
    w = Mul(w, R);
    w.m[12] = p.x; w.m[13] = p.y; w.m[14] = p.z;
}

struct Hero {
    uintptr_t skel = 0; int count = 0;
    std::vector<int> parent;
    std::vector<char> inHead;   // head_bind_jnt and everything under it (hair, helmet)
    std::vector<char> inNeck;   // the neck's lowest bone and everything under it (neck, head)
    int neck = -1;              // that bone: the neck's base on the chest
    // R_weapon_attach / L_weapon_attach, under each wrist: where the game hangs
    // what the hand holds (the sorceress's staff hangs on the left one).
    int weapon[2] = {-1, -1};
    // "shield_attach": the bone the game's shield effects name as their target (the Paladin's Holy Shield: targetbone shield_attach) - where the shield on the screen hangs. Under the arm, not under the
    // hand: the upper-body pass re-hangs it in its bind pose on its parent every pose, so a shield strapped only by L_weapon_attach never left the left arm on the screen (-1: the rig has none).
    int shield = -1;
    // two_hand_weapon_attach (the barbarian's rig, under the body, not a wrist): the game hangs a
    // two-handed weapon on it in some animations - walking with a great axe (2026-10-06).
    int twoHand = -1;
    std::vector<char> upper;    // spine_01 and everything under it: the body above the pelvis
    int upr[2] = {-1, -1}, lwr[2] = {-1, -1}, hand[2] = {-1, -1};   // [0] right, [1] left
    // first knuckles: index, middle, pinky - they give the hand's own axes
    int ind[2] = {-1, -1}, mid[2] = {-1, -1}, pnk[2] = {-1, -1};
    int head = -1, spine = -1, pelvis = -1;
    int eyes[2] = {-1, -1};     // eyeball bones, where the rig has them
    // [body] crouch: each leg's hip, knee and foot bones ([0] right, [1] left; -1 not found): what is bent again when the body
    // goes down with the camera (PoseBody, vr/crouch.h)
    int leg[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
    // Where this hero's eyes are, standing (model space y): the camera's height
    // per class. And the pelvis standing: how far above it the animation lifts
    // the hips is a jump (the barbarian's Leap), the camera goes up with it.
    float eyeY = 0.0f, pelvisY = 0.0f;
    // The bind pose (model space, from granny_bone::InverseWorld4x4 at +0x50),
    // what the body above the pelvis is held in instead of the animation.
    std::vector<Mat> bind;
    std::vector<char> inHand;   // under a hand (fingers, weapon): keeps the animation's grip
    std::vector<std::string> boneNames;   // the skeleton's bone names, by index (for the log: which bones the string anchor is made of)
    // The fingers of each hand, for the fist: chains of bones from the wrist
    // out (a metacarpal first where the rig has one - it is not bent).
    // kind: which finger this chain is by its first bone's name (bowhook::Kind: the Amazon's "R_ind_finger_01_bind_jnt" ...), -1 when the rig's
    // names do not say - the game's own draw pose (vr/bow_hook.h) is laid on the chains it can name.
    struct Finger { std::vector<int> bones; int firstBend = 0; bool thumb = false; int kind = -1; };
    std::vector<Finger> fingers[2];
    bool bindOk = false;
    int cls = -1;   // kHeroClasses, from the model's folder
    // [hands] forearm_twist: the rig's forearm twist bones (a name with "twist" under the forearm, not under the hand: the paladin's
    // and amazon's *_lwr_arm_twist_01/02), each with how far along the forearm it sits in the bind pose (0 the elbow, 1 the wrist).
    struct Twist { int bone; float t; };
    std::vector<Twist> twist[2];
    std::string twistNote;   // what was found, for the log (once per rig)
};

std::atomic<int> g_lockState{0};
std::atomic<int> g_heroClass{-1};
SRWLOCK g_namesLock = SRWLOCK_INIT;
std::map<uintptr_t, std::vector<std::string>> g_boneNames;   // per skeleton, for [debug] shrink_bone
std::atomic<float> g_attachSize[2];   // this pose's R/L_weapon_attach size relative to its wrist, as the game animates it
// [diag] a two-handed weapon while it is held (vrcam logs it): in the game's own pose, each attach
// bone against its own wrist compared with the grip (cm, deg), the two bones' skin origins apart
// (invBind x world: where each would draw the weapon) and each one's from the right wrist.
std::atomic<float> g_diag[14];
// The game's animation holds the weapon in the hand as the grip has it (its attach bone within 2 cm
// and 4 deg of the grip - standing; walking moves it 14 cm): HoldWeaponItem lets the weapon's own
// skeleton animate and remembers it then, and holds it in that pose otherwise.
std::atomic<bool> g_weaponCalm{true};
SRWLOCK g_rAttachLock = SRWLOCK_INIT;
Mat g_rAttach{};   // the hero's R_weapon_attach as we left it (model space), for WeaponEntityDiag
// and the whole pose, the game's and ours, with the bone names: which bone does the weapon follow?
std::vector<Mat> g_diagOld, g_diagNew;
int g_diagWrist = -1, g_diagAttach = -1;
uintptr_t g_diagSkel = 0;
std::atomic<float> g_eyeY{0.0f}, g_lift{0.0f}, g_rawLift{0.0f};
// [body] crouch, for vrcam's log: how far the body was last taken down (model units) and whether this hero's legs were found
// (1 both, 0 not, -1 not asked yet)
std::atomic<float> g_crouchApplied{0.0f};
std::atomic<int> g_crouchLegs{-1};
#if D2RVR_FIRST_PERSON
// [gameplay] shield_smite: the left weapon attach bone in the render world, as last posed (only while asked for)
ShieldPose g_shieldPose{};
ULONGLONG g_shieldAt = 0;
std::atomic<ULONGLONG> g_shieldWantedAt{0};
SRWLOCK g_twistNoteLock = SRWLOCK_INIT;
std::string g_twistNote;                // [hands] forearm_twist: the last rig's twist bones, for the log
std::atomic<int> g_shieldStrapped{0};   // [shield] forearm: 1 strapped this pose, -1 asked but not (hidden, no bind pose), 0 not asked
std::atomic<float> g_shieldAdjSeen[6] = {};                       // [shield] x y z (cm) pitch yaw roll (deg) as the last strap was given them (ShieldAdjNote)
std::atomic<float> g_shieldAdjMoveCm{0.0f}, g_shieldAdjTurnDeg{0.0f};   // ... and how far they moved / turned the shield from the plain strap
#endif
#if D2RVR_FIRST_PERSON
std::atomic<int> g_gunAxis{0};   // GunAxis
struct GunBoneOut { float o[3], ax[3][3]; ULONGLONG at; };
SRWLOCK g_gunBoneLock = SRWLOCK_INIT;
GunBoneOut g_gunBone{};
// [multiplayer] pose_share (formats 4, 6): both weapon attach bones as the last pose drew them (DrawnAttach), the head's turn-only frame; and both hands, elbows and
// the head bone, our model's units a metre (DrawnHands)
struct DrawnAttachOut {
    bool ok[2] = {false, false};
    float origin[2][3] = {}, rows[2][3][3] = {};
    int main = 0;
    bool handsOk = false;
    float hand[2][3] = {}, handRows[2][3][3] = {}, elbow[2][3] = {}, headBone[3] = {}, upm = 0.0f;
    ULONGLONG at = 0;
};
SRWLOCK g_drawnAttachLock = SRWLOCK_INIT;
DrawnAttachOut g_drawnAttach;
// [hands] xbow_support_model / [debug] xbow_barrel_line: the hero's crossbow's barrel in its attach bone's frame, as its own skeleton
// was last posed (NoteCrossbowBarrel), and what was drawn of it - under g_gunBoneLock.
//   g_xbowBarrel    from the item's own transform (its TransformComponent's local, relative to its parent) - what [bow] aim_along_hands
//                   uses for a bow. For the crossbow it is NOT how it is drawn (test-41, 2026-10-16: the line crossed the model at ~45 deg,
//                   the palm's place at the right limb's tip): kept for the log's comparison and the debug drawing (orange) only.
//   g_xbowRendered  from the item's world matrix as the renderer makes it (0xE3A8C0, potion_hide's hook -> XbowRendered) against our
//                   attach bone in the world at the pose (g_xbowAttach) - bearstonem/d2r-pcvr's way (skeletons.cpp CrossbowRendered,
//                   crossbow_pose.h RenderedGeometry): it holds whatever the renderer puts between the item and the wrist. The hand uses this.
struct XbowBarrelOut { xbowgrip::M inAttach; uint32_t gen = 0; ULONGLONG at = 0; };
XbowBarrelOut g_xbowBarrel{}, g_xbowRendered{};
// the renderer's barrel kept per weapon and hero model (rigid: the crossbow's own), and whose ((HeroGen << 32) | weapon key) - the pose thread's
XbowBarrelOut g_xbowHeldBarrel{};
uint64_t g_xbowHeldKey = 0;
// [hands] xbow_handover: the support hand holds the crossbow now (its latch, last pose), and for which weapon, model and holder
bool g_xbowSupportHeld = false;
uint64_t g_xbowSupportHeldKey = 0;
// what XbowRendered needs of the crossbow's skeleton (NoteCrossbowBarrel) and of our pose (PoseBody): its cog's place in its world
// array, the barrel in the cog's frame; our attach bone in the world, and the model units per metre
struct XbowSource { uintptr_t self = 0; int cog = -1, count = 0; xbowgrip::M barrelInCog; uint32_t gen = 0; ULONGLONG at = 0; };
XbowSource g_xbowSource{};
struct XbowAttach { xbowgrip::M world; float upm = 0.0f; uint32_t gen = 0; ULONGLONG at = 0; };
XbowAttach g_xbowAttach{};
std::atomic<uint32_t> g_xbowEntity{0xFFFFFFFFu};             // the hero's crossbow's entity (XbowRendered's one cheap test)
struct XbowDebugOut { float a[3], b[3], p[3]; bool rendered; ULONGLONG at; };
XbowDebugOut g_xbowDebug{};
// [debug] xbow_barrel_line: where the shot goes - from the muzzle along the direction the aim uses (the drawn barrel, or the hand-set
// line), to where it meets the floor or 8 m out; same frame as the barrel's. barrel: the drawn barrel's direction (else the hand-set line).
struct XbowRayOut { float a[3], b[3]; bool barrel, floor; ULONGLONG at; };
XbowRayOut g_xbowRay{};
// [debug] xbow_barrel_line: where the left palm's middle (wrist to middle knuckle, halfway) really is as the pose leaves it, and how far
// from its place - a white ring on the overlay, and the log every 3 s (same frame as the barrel's)
struct XbowPalmOut { float p[3]; float cm; ULONGLONG at; };
// [hands] xbow_support_game: the game's own fore-grip on the crossbow - in its own idle pose the game holds the crossbow in both hands
// (palms 0.80 model units apart, "the hold"): the hand it does NOT hang the crossbow on has its palm's middle on the crossbow's fore-grip.
// Learned from the game's pose (oldW), in the crossbow attach bone's frame, averaged over the first samples per weapon and hero model;
// and how that palm faces against the barrel (cup under the stock / wrap it from the side / over the top), for the log.
// The game's two hands on its crossbow (0.170: "the grip point broke"): the hand it HANGS the crossbow on (the left) holds the fore-grip
// mid-stock under the prod; its other hand (the right) holds the trigger grip at the back (the log: 37 cm behind the front of the stock).
// palm/facing/across: the fore-grip hand's; trigPalm: the trigger hand's palm's middle - both in the attach bone's frame.
struct XbowForeGrip { uint64_t key = 0; int n = 0; V3 sum{}; V3 palm{}; V3 facing{}, across{}; V3 trigSum{}, trigPalm{}; bool told = false; };
constexpr int kForeGripSamples = 30;
XbowForeGrip g_xbowFore;   // the pose thread's
XbowPalmOut g_xbowPalm{};
SRWLOCK g_xbowNoteLock = SRWLOCK_INIT;
std::string g_xbowNote;                                       // for vrcam's log (XbowNoteTake)
void XbowNote(const std::string& line) {
    AcquireSRWLockExclusive(&g_xbowNoteLock);
    if (g_xbowNote.size() < 3000) g_xbowNote += (g_xbowNote.empty() ? "" : "\n") + line;
    ReleaseSRWLockExclusive(&g_xbowNoteLock);
}
std::atomic<bool> g_xbowBarrelOn{false};                     // asked for (Set): a crossbow held the gun's way in F4 with either key on
std::atomic<const char*> g_xbowBarrelWhy{"not asked for"};   // the barrel's reading, for the log (XbowBarrelWhy)
// The posed weapon in world units (PosedWeapon, melee by touch); written by NotePosedWeapon at the end of a pose.
SRWLOCK g_posedLock = SRWLOCK_INIT;
PosedWeaponPose g_posed{};
ULONGLONG g_posedAt = 0;
// [debug] the line the left hand takes the weapon by (staff, spear, sword), the same frame
struct GrabLineOut { float a[3], b[3]; ULONGLONG at; };
GrabLineOut g_grabLine{};
// [bow] aim_distance_by_arm: the shoulders and the reach as the last pose drew them (ArmReach)
struct ArmReachOut { float shoulder[3], length; ULONGLONG at; };
SRWLOCK g_armReachLock = SRWLOCK_INIT;
ArmReachOut g_armReach[2] = {};
// [gameplay] leap_arc_head: the belly as the last pose drew it, the same frame (BodyBelly)
struct BellyOut { float at3[3]; ULONGLONG at; };
BellyOut g_belly{};
// [weight] grip_fit, for the debug: the grasp point on the axis and the palm's middle as drawn, per hand, the same frame (GripMarks)
struct GripMarkOut { float grasp[3], palm[3]; ULONGLONG at; };
SRWLOCK g_gripMarkLock = SRWLOCK_INIT;
GripMarkOut g_gripMark[2] = {};
#endif
std::atomic<ULONGLONG> g_eyeAt{0};
std::atomic<float> g_poseYaw[3];

SRWLOCK g_heroLock = SRWLOCK_INIT;
// Per SkeletonInstance, keyed with its skeleton too: an instance address the
// game frees and reuses for another model must not keep the old verdict.
// Held by a shared pointer: each pose of the hero takes the pointer, not a copy of the rig (its bone names, bind pose, finger chains - 3-6 us a pose, 2026-10-21).
std::map<std::pair<uintptr_t, uintptr_t>, std::shared_ptr<const Hero>> g_heroes;   // null: not the hero
// [hands] doll_rebuild: a player's rig not learned whole yet (no rig by name, or its bind pose did not read) - learned again (Arms), under g_heroLock
struct HeroRetry { ULONGLONG first = 0, last = 0; int tries = 0; };
std::map<std::pair<uintptr_t, uintptr_t>, HeroRetry> g_heroRetry;

#if D2RVR_FIRST_PERSON
// The grip: what the hands hold, relative to the hands, taken from the game's
// animation once - while the hero does not attack - and kept for that weapon.
// Taken live, the walk slid the right hand along a staff and turned it, and a
// blow twisted every weapon in the fist.
struct Grip {
    int type = -1;
    bool ok = false;
    Mat attach[2];       // R/L_weapon_attach relative to its wrist
    Mat inStaff[2];      // each wrist relative to the staff (the attach bone that holds it)
    // Each palm's middle (wrist to middle knuckle, halfway) in the staff's
    // frame: the shaft runs through both fists, so through these two points.
    float palm[2][3] = {};
    // staffHandAuto: the wrist the weapon hangs on, read from the attach bones' sizes
    // (the game shrinks one that holds nothing - as it does what leaves the hand);
    // -1 when they did not tell.
    int hand = -1;
    float scale[2] = {};
};
SRWLOCK g_gripLock = SRWLOCK_INIT;
Grip g_grip;
// [weight] (vr/weight.cpp): the weapon relative to each hand that holds it (model space, the attach bone
// against the wrist), taken while that hand is fully on it and the other one carries it - what it is
// carried by once the other lets go. grabs: the hand's take it was taken for (weight::Out::grabs).
struct WeightRel {
    Mat rel[2]; uint32_t grabs[2] = {}; bool ok[2] = {}; Mat body; bool bodyOk = false;   // body: against the weight model's body
    // [weight] grip_fit_like_base: how the base hand (the game's hand, as placed) sits on the shaft - its palm's offset from the axis across and up the hand (cm) and the lean of its
    // knuckle line (degrees), measured each pose while it is fully on the weapon: what a hand that takes the weapon is given (vr/grip_fit.h Relation). For weapon type baseType.
    gripfit::Tuning base;
    bool baseOk = false;
    int baseType = -1;
    // ... and the same grip as the shaft lies against the hand (vr/grip_fit.h AxisOf, in the hand's own frame, metres): what the grip library lays a hand that takes the weapon by
    gripfit::AxisInHand baseAxis;
};
SRWLOCK g_weightLock = SRWLOCK_INIT;
WeightRel g_weightRel;
// [weight] second_lets_go: the second weapon's attach bone against the weight model's body of it (taken each pose it is in its hand), so it is drawn where that body is when let go
struct WeightRel2 { Mat body; bool ok = false; bool handOver = false; };   // handOver: measured once in the first's hand (again each frame would chase the hand it lays)
WeightRel2 g_weightRel2;
#endif

SRWLOCK g_inLock = SRWLOCK_INIT;
Input g_in;
std::atomic<FreshFn> g_fresh{nullptr};
std::atomic<RoomShiftFn> g_freshShift{nullptr};   // [openxr] roomscale_body
std::atomic<FreshHandsFn> g_freshHands{nullptr};
std::atomic<GrabHandsFn> g_grabHands{nullptr};   // [weight] grab_bodies (SetGrabHands)
// [weight] grab_wrap: the drawn palm of a holding hand (GrabPalmDrawn), written by the pose, read by grab_wire
SRWLOCK g_grabPalmLock = SRWLOCK_INIT;
float g_grabPalm[2][3] = {};
const char* g_grabPalmHow[2] = {"", ""};
ULONGLONG g_grabPalmAt[2] = {};
void NoteGrabPalm(int side, const float head[3], const char* how) {
    AcquireSRWLockExclusive(&g_grabPalmLock);
    for (int k = 0; k < 3; ++k) g_grabPalm[side & 1][k] = head[k];
    g_grabPalmHow[side & 1] = how;
    g_grabPalmAt[side & 1] = GetTickCount64();
    ReleaseSRWLockExclusive(&g_grabPalmLock);
}
std::atomic<GripHandStepFn> g_gripHandStep{nullptr};   // [weight] grip_model 1: the grip library's hand step (SetGripHandStep)
std::atomic<GripAbiFn> g_gripAbi{nullptr};

// ---- The hero's transform in the renderer's ECS (docs/doll_binding.md, 2026-10-06) ----
// The renderer keeps its world in an entt registry, a global (D2RLoader layout 0x2677170 -
// Renderer's getter 0x9124E0 returns it). Every model is an entity; its SkeletonComponent is
// a shared_ptr to the SkeletonInstance we hook (the pose job 0xEFAA3A reads it, then calls
// ComputeSelfWorldPose), and its TransformComponent (0xD0 bytes) holds three matrices:
//   +0x00 local - what the game sets for the unit before the frame's poses: this frame's
//   +0x40 world - Renderer::UpdateSystems "UpdateTransform" (0xE3A8C0), AFTER WaitForPoseUpdate:
//                 at the left pass's pose still the last frame's
//   +0x80 the world before that,  +0xC0 the parent entity (0xFFFFF in its low 20 bits = none,
//                 then world = local)
// UpdateModelInstanceBounds (0xE38010) copies +0x40 into the model object's +0x260 - the
// "late" copy 0xF3F970 writes. The whole-memory search found these same matrices among its
// 25-40 (the stride-0x40 "early" group = one TransformComponent; 0x650 apart = model objects).
// The registry's pools: a vector at +0 / +8 of 0x28-byte entries, the storage at entry +0x20.
// A storage (0x98 bytes): vtable, sparse pages +0x08/+0x10 (u32[256] each, page = (e >> 8) &
// 0xFFF, slot = e & 0xFF, dense index in the low 20 bits), packed entities +0x20/+0x28 (u32),
// components +0x38 (contiguous - the array moves when it grows, so it is re-read every time).
// A pool sits at its type's sequence number, which entt's type_seq<T>::value() gives - the
// game's own function (0x20B8F0 for TransformComponent, its storage made in 0x2035B0, vtable
// 0x1CE0958; 0x67DE50 for SkeletonComponent, made in 0x66D4F0, vtable 0x1D4A000). The storages
// are taken from there and told afterwards by the vtables they had then. The vtables were the
// key until 2026-10-06, but another build cannot find them by a pattern (every storage<T> is
// made by the same code); type_seq<T> is found next to T's name hash (cleanroom/sigscan).
constexpr uint64_t RVA_ECS_REGISTRY = 0x2677170;
constexpr uint64_t RVA_TYPE_SEQ_TRANSFORM = 0x20B8F0, RVA_TYPE_SEQ_SKELETON = 0x67DE50;
constexpr uint64_t RVA_TYPE_SEQ_MODEL = 0x1DBE40;   // type_seq<ModelComponent> ([weight] monster_bodies: a model's meshes, vr/monster_rig.h)
constexpr uint64_t RVA_TYPE_SEQ_CHILDREN = 0x799040, RVA_TYPE_SEQ_PART = 0x67E1D0;   // type_seq<AttachedChildrenComponent>, <UnitPartComponent> (a unit's parts)
constexpr uint32_t kModelCompSize = 0x88;
constexpr uint32_t kTransformSize = 0xD0, kSkeletonCompSize = 0x10;

std::atomic<uintptr_t> g_gameBase{0};
std::atomic<bool> g_trackHero{false};      // vrcam wants the hero known even while no pose is changed
std::atomic<uintptr_t> g_heroSelf{0};      // the hero's SkeletonInstance, last posed
std::atomic<uint32_t> g_heroGen{0};        // bumped when it changes (a weapon swap rebuilds his model)
std::atomic<ULONGLONG> g_heroSeen{0};

// [debug] doll_trace: the hero's rig let go - not posed as ours - and taken again, noted here for vrcam's tick to log (DollTakeNote). Let go: the rig posed as
// ours last is turned away (OursNow says why), a player model's rig is not learned, or no rig was ours for 150 ms and the one met is turned away.
SRWLOCK g_dollNoteLock = SRWLOCK_INIT;
std::string g_dollNote;
std::atomic<bool> g_dollLetGo{false};
std::atomic<ULONGLONG> g_dollLetGoAt{0};
std::atomic<uintptr_t> g_dollAlongTold{0};   // the last rig told as posed along
void NoteDoll(const char* fmt, ...) {
    char b[600];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    AcquireSRWLockExclusive(&g_dollNoteLock);
    if (g_dollNote.size() < 4000) g_dollNote += (g_dollNote.empty() ? "" : "\n") + std::string(b);
    ReleaseSRWLockExclusive(&g_dollNoteLock);
}

void NoteHero(uintptr_t self) {
    g_heroSeen.store(GetTickCount64());
    if (g_heroSelf.exchange(self) != self) g_heroGen.fetch_add(1);
}

namespace ecs {
SRWLOCK g_lock = SRWLOCK_INIT;
uintptr_t g_xs = 0, g_ss = 0;               // the two storages, found by type index
uintptr_t g_vtX = 0, g_vtS = 0;             // and their vtables then
uintptr_t g_cachedSelf = 0;
uint32_t g_cachedEntity = 0;
ULONGLONG g_lastStorageLook = 0;
const char* g_why = "";                     // why the chain does not read (for vrcam's log)

bool IsStorage(uintptr_t st, uintptr_t vtWas) {
    uintptr_t vt = 0;
    return st && vtWas && Get(st, &vt) && vt == vtWas;
}

bool FindStorages() {
    const uintptr_t reg = d2rsig::Addr(RVA_ECS_REGISTRY);
    const uintptr_t seqX = d2rsig::Addr(RVA_TYPE_SEQ_TRANSFORM), seqS = d2rsig::Addr(RVA_TYPE_SEQ_SKELETON);
    if (!reg || !seqX || !seqS) { g_why = "the renderer's registry is not known in this game build"; return false; }
    uintptr_t b = 0, e = 0;
    if (!Get(reg, &b) || !Get(reg + 8, &e) || e <= b || (e - b) % 0x28 || (e - b) / 0x28 > 4096) { g_why = "the registry's pools do not read"; return false; }
    const size_t n = (e - b) / 0x28;
    // Both types are in use once the hero has been posed, so their numbers are given already.
    const uint32_t ix = (uint32_t)((int (*)())seqX)(), is = (uint32_t)((int (*)())seqS)();
    uintptr_t xs = 0, ss = 0;
    if (ix >= n || is >= n || ix == is || !Get(b + ix * 0x28 + 0x20, &xs) || !Get(b + is * 0x28 + 0x20, &ss) || !xs || !ss) {
        g_why = "no TransformComponent / SkeletonComponent storage in the registry";
        return false;
    }
    uintptr_t vx = 0, vs = 0;
    if (!Get(xs, &vx) || !Get(ss, &vs) || !vx || !vs || vx == vs) { g_why = "the registry's storages do not read"; return false; }
    g_xs = xs; g_ss = ss; g_vtX = vx; g_vtS = vs;
    return true;
}

// The dense index of entity e in storage st, checked against the packed list.
bool Dense(uintptr_t st, uint32_t e, uint32_t* idx) {
    uintptr_t sb = 0, se = 0, pb = 0, pe = 0, page = 0;
    if (!Get(st + 0x08, &sb) || !Get(st + 0x10, &se) || se < sb) return false;
    const uint64_t pi = (e >> 8) & 0xFFF;
    if (pi >= (se - sb) / 8 || !Get(sb + pi * 8, &page) || !page) return false;
    uint32_t v = 0;
    if (!Get(page + (e & 0xFF) * 4, &v)) return false;
    v &= 0xFFFFF;
    if (v == 0xFFFFF || !Get(st + 0x20, &pb) || !Get(st + 0x28, &pe) || pe < pb || v >= (pe - pb) / 4) return false;
    uint32_t back = 0;
    if (!Get(pb + (uintptr_t)v * 4, &back) || back != e) return false;
    *idx = v;
    return true;
}

bool SkeletonOf(uint32_t e, uintptr_t* self) {
    uint32_t i = 0; uintptr_t cb = 0;
    return Dense(g_ss, e, &i) && Get(g_ss + 0x38, &cb) && cb && Get(cb + (uintptr_t)i * kSkeletonCompSize, self);
}

// The entity whose SkeletonComponent holds `self`: one pass over the storage (only for a new instance).
bool EntityOf(uintptr_t self, uint32_t* e) {
    uintptr_t pb = 0, pe = 0, cb = 0;
    if (!Get(g_ss + 0x20, &pb) || !Get(g_ss + 0x28, &pe) || pe <= pb || !Get(g_ss + 0x38, &cb) || !cb) return false;
    const size_t n = (pe - pb) / 4;
    if (n > 200000) return false;
    std::vector<uint64_t> comps(n * (kSkeletonCompSize / 8));
    std::vector<uint32_t> packed(n);
    if (!SafeCopy(comps.data(), (const void*)cb, n * kSkeletonCompSize) || !SafeCopy(packed.data(), (const void*)pb, n * 4)) return false;
    for (size_t i = 0; i < n; ++i)
        if (comps[i * (kSkeletonCompSize / 8)] == self) { *e = packed[i]; return true; }
    return false;
}

// Where a player rig stands in the world (OursNow): its TransformComponent's translation, the
// local matrix (+0x00) for an entity with no parent - as the hero's (HeroTransform) - else the
// world one (+0x40). Its entity is kept per instance; one not found is looked for again after 500 ms.
constexpr uint32_t kNoEntity = 0xFFFFFFFFu;
std::map<uintptr_t, std::pair<uint32_t, ULONGLONG>> g_bodyEntity;   // SkeletonInstance -> entity (or none), when looked up
// noEntity: the storages read, but no entity holds this instance - not a rig of the renderer's world (a menu's model).
// m16 (optional): the whole matrix (row vectors, the translation in 12..14: model -> world, the look-at's units) - [multiplayer] pose_share the other heroes' bodies and travel.
bool BodyPlace(uintptr_t self, float* x, float* z, bool* noEntity = nullptr, ULONGLONG retryMs = 500, float* m16 = nullptr) {
    if (noEntity) *noEntity = false;
    AcquireSRWLockExclusive(&g_lock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_lock); } } unlock;
    const ULONGLONG now = GetTickCount64();
    if (!IsStorage(g_xs, g_vtX) || !IsStorage(g_ss, g_vtS)) {
        if (now - g_lastStorageLook < 500) return false;
        g_lastStorageLook = now;
        if (!FindStorages()) return false;
        g_cachedSelf = 0;
        g_bodyEntity.clear();
    }
    uint32_t e = kNoEntity;
    uintptr_t held = 0;
    const auto it = g_bodyEntity.find(self);
    if (it != g_bodyEntity.end() && it->second.first != kNoEntity && SkeletonOf(it->second.first, &held) && held == self) {
        e = it->second.first;
    } else {
        if (it != g_bodyEntity.end() && it->second.first == kNoEntity && now - it->second.second < retryMs) { if (noEntity) *noEntity = true; return false; }
        if (!EntityOf(self, &e)) e = kNoEntity;
        if (g_bodyEntity.size() > 256) g_bodyEntity.clear();
        g_bodyEntity[self] = {e, now};
        if (e == kNoEntity) { if (noEntity) *noEntity = true; return false; }
    }
    uint32_t i = 0, parent = kNoEntity;
    uintptr_t cb = 0;
    if (!Dense(g_xs, e, &i) || !Get(g_xs + 0x38, &cb) || !cb) return false;
    const uintptr_t comp = cb + (uintptr_t)i * kTransformSize;
    if (!Get(comp + 0xC0, &parent)) return false;
    float t[3];
    const uintptr_t mat = comp + ((parent & 0xFFFFF) == 0xFFFFF ? 0x00 : 0x40);
    if (!SafeCopy(t, (const void*)(mat + 12 * 4), sizeof t)) return false;
    if (!std::isfinite(t[0]) || !std::isfinite(t[2])) return false;
    if (m16 && !SafeCopy(m16, (const void*)mat, 16 * sizeof(float))) return false;
    *x = t[0]; *z = t[2];
    return true;
}
}  // namespace ecs

// player: the model is a player's (/character/player/), learned or not.
bool Learn(uintptr_t self, Hero* h, bool* player = nullptr) {
    if (player) *player = false;
    uintptr_t modelInst = 0, skel = 0, nameObj = 0, nameP = 0;
    if (!Get(self + 0x08, &modelInst) || !Get(modelInst + 0x08, &skel)) return false;
    Get(self, &nameObj); Get(nameObj + 8, &nameP);
    const std::string model = Str(nameP);
    const size_t at = model.find("/character/player/");
    if (at == std::string::npos) return false;
    if (player) *player = true;
    h->cls = -1;
    for (int c = 0; c < 8; ++c)
        if (model.compare(at + 18, strlen(kHeroClasses[c]) + 1, std::string(kHeroClasses[c]) + "/") == 0) h->cls = c;
    int32_t count = 0; uintptr_t bones = 0;
    if (!Get(skel + 8, &count) || !Get(skel + 0xC, &bones) || count <= 0 || count > 1024) return false;
    h->skel = skel; h->count = count; h->parent.assign(count, -1); h->inHead.assign(count, 0); h->upper.assign(count, 0);
    std::vector<char> neckNamed(count, 0);
    std::vector<std::string> names(count);
    struct KeepNames { uintptr_t skel; std::vector<std::string>* v; ~KeepNames() {
        AcquireSRWLockExclusive(&g_namesLock); if (g_boneNames.size() > 256) g_boneNames.clear(); g_boneNames[skel] = *v; ReleaseSRWLockExclusive(&g_namesLock); } } keep{skel, &names};
    for (int i = 0; i < count; ++i) {
        uintptr_t nm = 0; int32_t p = -1;
        Get(bones + i * 0xA4, &nm); Get(bones + i * 0xA4 + 8, &p);
        h->parent[i] = p;
        const std::string n = Str(nm);
        names[i] = n;
        // Three rigs among the heroes (F10 dumps, 2026-10-03):
        //   paladin, amazon     R_upr_arm_bind_jnt ... head_bind_jnt, spine_01_bind_jnt,
        //                       fingers R_ind/mid/pnk_finger_01_bind_jnt
        //   sorceress           R_UpperArm, R_ForeArm, R_Wrist, head, spine_01,
        //                       fingers R_finger_A..D_A: A the index (nearest the
        //                       thumb, the knuckles run A-B-C-D), B middle, D little
        //   assassin            shoulder_R_JNT, elbow_R_JNT, wrist_R_JNT, head_M_JNT,
        //                       spine_01_M_JNT, hand_index/middle/pinky_01_R_JNT
        // The first bone with a matching name wins: the assassin also has a leaf
        // called "head" under head_M_JNT, and taking that one would hide nothing.
        // A hero whose rig matches none is skipped - no hidden head, no held body.
        struct Alias { int* slot; const char* names[3]; };
        const Alias kAliases[] = {
            {&h->upr[0], {"R_upr_arm_bind_jnt", "R_UpperArm", "shoulder_R_JNT"}},
            {&h->upr[1], {"L_upr_arm_bind_jnt", "L_UpperArm", "shoulder_L_JNT"}},
            {&h->lwr[0], {"R_lwr_arm_bind_jnt", "R_ForeArm", "elbow_R_JNT"}},
            {&h->lwr[1], {"L_lwr_arm_bind_jnt", "L_ForeArm", "elbow_L_JNT"}},
            {&h->hand[0], {"R_hand_bind_jnt", "R_Wrist", "wrist_R_JNT"}},
            {&h->hand[1], {"L_hand_bind_jnt", "L_Wrist", "wrist_L_JNT"}},
            {&h->ind[0], {"R_ind_finger_01_bind_jnt", "R_finger_A_A", "hand_index_01_R_JNT"}},
            {&h->ind[1], {"L_ind_finger_01_bind_jnt", "L_finger_A_A", "hand_index_01_L_JNT"}},
            {&h->mid[0], {"R_mid_finger_01_bind_jnt", "R_finger_B_A", "hand_middle_01_R_JNT"}},
            {&h->mid[1], {"L_mid_finger_01_bind_jnt", "L_finger_B_A", "hand_middle_01_L_JNT"}},
            {&h->pnk[0], {"R_pnk_finger_01_bind_jnt", "R_finger_D_A", "hand_pinky_01_R_JNT"}},
            {&h->pnk[1], {"L_pnk_finger_01_bind_jnt", "L_finger_D_A", "hand_pinky_01_L_JNT"}},
            {&h->head, {"head_bind_jnt", "head_M_JNT", "head"}},
            {&h->spine, {"spine_01_bind_jnt", "spine_01", "spine_01_M_JNT"}},
            {&h->weapon[0], {"R_weapon_attach", "R_weapon_attach", "R_weapon_attach"}},
            {&h->weapon[1], {"L_weapon_attach", "L_weapon_attach", "L_weapon_attach"}},
            {&h->shield, {"shield_attach", "shield_attach", "shield_attach"}},
            {&h->twoHand, {"two_hand_weapon_attach", "two_hand_weapon_attach", "two_hand_weapon_attach"}},
        };
        for (const Alias& a : kAliases) {
            if (*a.slot >= 0) continue;
            if (n == a.names[0] || n == a.names[1] || n == a.names[2]) { *a.slot = i; break; }
        }
        {   // the eyeballs: R_eye_bind_jnt, R_eye, eye_L_JNT... - not lids, brows or lashes
            std::string lo = n;
            for (char& ch : lo) ch = (char)tolower((unsigned char)ch);
            neckNamed[i] = lo.find("neck") != std::string::npos && lo.find("necklace") == std::string::npos;
            if (lo.find("eye") != std::string::npos && lo.find("lid") == std::string::npos &&
                lo.find("brow") == std::string::npos && lo.find("lash") == std::string::npos) {
                if (h->eyes[0] < 0) h->eyes[0] = i; else if (h->eyes[1] < 0) h->eyes[1] = i;
            }
        }
        if (h->head >= 0 && (i == h->head || (p >= 0 && h->inHead[p]))) h->inHead[i] = 1;
        if (h->spine >= 0 && (i == h->spine || (p >= 0 && h->upper[p]))) h->upper[i] = 1;
    }
    const bool arms = h->upr[0] > 0 && h->lwr[0] > 0 && h->hand[0] > 0 && h->upr[1] > 0 && h->lwr[1] > 0 && h->hand[1] > 0;
    if (!arms) return false;
    {   // what a held weapon or shield could hang on in this rig, told for the first few heroes: every bone named weapon / attach / shield / buckler / hold, with its parent
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 6) {
            std::string list;
            for (int i = 0; i < count; ++i) {
                std::string lo = names[i];
                for (char& ch : lo) ch = (char)tolower((unsigned char)ch);
                if (lo.find("weapon") == std::string::npos && lo.find("attach") == std::string::npos && lo.find("shield") == std::string::npos &&
                    lo.find("buckler") == std::string::npos && lo.find("hold") == std::string::npos) continue;
                const int p = h->parent[i];
                list += (list.empty() ? "" : ", ") + names[i] + " #" + std::to_string(i) + " (under " + (p >= 0 ? names[p] : std::string("nothing")) + ")";
            }
            char head[200];
            snprintf(head, sizeof head, "hero rig %s: %d bones; the shield is strapped by the left attach bone %s (#%d) - bones that could carry a weapon or a shield: ",
                     h->cls >= 0 ? kHeroClasses[h->cls] : "?", count, h->weapon[1] >= 0 ? names[h->weapon[1]].c_str() : "NOT FOUND", h->weapon[1]);
            // (the log line has room for some 300 characters: the list in pieces, so that none of it is cut off - which bone the shield really hangs on is read from it)
            {   // the bone the shield effects name: whether this rig has it, and what it hangs under (the strap acts on it as well as on the left attach bone)
                char sh[260];
                snprintf(sh, sizeof sh, "hero rig %s: shield_attach %s%s%s%s", h->cls >= 0 ? kHeroClasses[h->cls] : "?", h->shield >= 0 ? "found, #" : "NOT FOUND (the shield is strapped by the left attach bone only)",
                         h->shield >= 0 ? std::to_string(h->shield).c_str() : "", h->shield >= 0 ? ", under " : "",
                         h->shield >= 0 && h->parent[h->shield] >= 0 ? names[h->parent[h->shield]].c_str() : "");
                weight::Say(sh);
            }
            if (list.empty()) {
                weight::Say((std::string(head) + "none by name").c_str());
            } else {
                size_t at = 0;
                int piece = 0;
                while (at < list.size()) {
                    size_t end = std::min(list.size(), at + 200);
                    if (end < list.size()) {
                        const size_t comma = list.rfind(", ", end);
                        if (comma != std::string::npos && comma > at) end = comma + 2;
                    }
                    const std::string part = list.substr(at, end - at);
                    weight::Say((std::string(piece == 0 ? head : "hero rig (cont.): ") + part).c_str());
                    at = end;
                    ++piece;
                }
            }
        }
    }
    // Forearm twist bones: named so, under the forearm and not under the hand; where along the forearm once the bind pose is read.
    for (int side = 0; side < 2; ++side) {
        int found = 0;
        for (int i = 0; i < count; ++i) {
            std::string lo = names[i];
            for (char& ch : lo) ch = (char)tolower((unsigned char)ch);
            if (lo.find("twist") == std::string::npos) continue;
            bool underArm = false, underHand = false;
            for (int b = h->parent[i]; b >= 0; b = h->parent[b]) {
                if (b == h->hand[side]) underHand = true;
                if (b == h->lwr[side]) { underArm = true; break; }
            }
            if (underArm && !underHand) { h->twist[side].push_back({i, 0.5f}); ++found; }
            else if (lo.find(side == 0 ? "r_" : "l_") == 0 || lo.find(side == 0 ? "_r_" : "_l_") != std::string::npos ||
                     lo.find(side == 0 ? "right" : "left") != std::string::npos)
                h->twistNote += std::string(h->twistNote.empty() ? "" : ", ") + names[i] + " (under " +
                                (h->parent[i] >= 0 ? names[h->parent[i]] : std::string("nothing")) + ", not the forearm: left alone)";
        }
        (void)found;
    }

    // The neck: from the head down through the bones named neck (neck_bind_jnt,
    // C_Neck_neck1..3, neck_main_00_M_JNT) to the lowest one. Hidden with the
    // head: a camera right at the eyes otherwise looks down onto its stump.
    h->inNeck.assign(count, 0);
    if (h->head >= 0) {
        for (int b = h->parent[h->head]; b >= 0 && neckNamed[b]; b = h->parent[b]) h->neck = b;
        const int root = h->neck >= 0 ? h->neck : h->head;
        for (int i = 0; i < count; ++i) {
            const int p = h->parent[i];
            if (i == root || (p >= 0 && h->inNeck[p])) h->inNeck[i] = 1;
        }
    }

    h->boneNames = names;
    // Fingers: each child of a wrist except the weapon attach and the _END
    // marker, followed out while a bone has exactly one child. Thumbs are
    // tmb (paladin rig) or thumb (the other two).
    for (int side = 0; side < 2; ++side) {
        auto lower = [&](int b) { std::string lo = names[b]; for (char& ch : lo) ch = (char)tolower((unsigned char)ch); return lo; };
        auto children = [&](int b) { std::vector<int> k; for (int i = 0; i < count; ++i) if (h->parent[i] == b) k.push_back(i); return k; };
        for (const int root : children(h->hand[side])) {
            const std::string lo = lower(root);
            if (lo.find("weapon") != std::string::npos || lo.find("_end") != std::string::npos) continue;
            Hero::Finger f;
            f.thumb = lo.find("tmb") != std::string::npos || lo.find("thumb") != std::string::npos;
            f.firstBend = lo.find("metacarpal") != std::string::npos ? 1 : 0;
            f.kind = bowhook::KindOf(names[root]);
            for (int b = root;;) {
                f.bones.push_back(b);
                const std::vector<int> k = children(b);
                if (k.size() != 1) break;
                b = k[0];
            }
            if ((int)f.bones.size() > f.firstBend + 1) h->fingers[side].push_back(f);
        }
    }

    h->inHand.assign(count, 0);
    for (int i = 0; i < count; ++i) {
        const int p = h->parent[i];
        if (p >= 0 && (p == h->hand[0] || p == h->hand[1] || h->inHand[p])) h->inHand[i] = 1;
    }
    // Bind pose, checked against the posed head: same height give or take a
    // crouch, every matrix finite with unit-ish axes. Anything else and the
    // lock stays off rather than throwing the body somewhere.
    h->bind.resize(count);
    bool ok = h->head >= 0 && h->spine >= 0;
    for (int i = 0; i < count && ok; ++i) {
        Mat inv{};
        ok = SafeCopy(inv.m, (const void*)(bones + i * 0xA4 + 0x50), 64);
        for (float f : inv.m) ok = ok && std::isfinite(f);
        if (!ok) break;
        h->bind[i] = InvAffine(inv);
        for (int r = 0; r < 3 && ok; ++r) {
            const float l = Len({h->bind[i].m[r*4], h->bind[i].m[r*4+1], h->bind[i].m[r*4+2]});
            ok = l > 0.5f && l < 2.0f;
        }
    }
    uintptr_t world = 0, mats = 0; float headNow[16] = {};
    if (ok && Get(self + 0x20, &world) && Get(world + 4, &mats) && SafeCopy(headNow, (const void*)(mats + h->head * 64), 64))
        ok = fabsf(h->bind[h->head].m[13] - headNow[13]) < 1.5f && h->bind[h->head].m[13] > 2.0f;
    else ok = false;
    h->bindOk = ok;
    g_lockState.store(ok ? 1 : -1);
    for (int side = 0; side < 2; ++side) {
        const V3 E = Pos(h->bind[h->lwr[side]]), W = Pos(h->bind[h->hand[side]]), EW = Sub(W, E);
        const float ll = Dot(EW, EW);
        for (Hero::Twist& t : h->twist[side]) {
            if (ok && ll > 1e-8f) t.t = std::clamp(Dot(Sub(Pos(h->bind[t.bone]), E), EW) / ll, 0.0f, 1.0f);
            h->twistNote += std::string(h->twistNote.empty() ? "" : ", ") + names[t.bone] + " at " + std::to_string((int)std::lround(t.t * 100.0f)) + "%";
        }
    }
    if (h->twistNote.empty()) h->twistNote = "none - the forearm bone itself turns";
    AcquireSRWLockExclusive(&g_twistNoteLock); g_twistNote = h->twistNote; ReleaseSRWLockExclusive(&g_twistNoteLock);

    // Standing heights from the bind pose (the animation's would bob with the
    // stride). Eyes 5.65 (sorceress) .. 7.04 (druid) in the F10 dumps; a rig
    // without eyeballs gets them a quarter unit above the head joint, which is
    // how far they sit in the rigs that have both.
    h->pelvis = h->spine >= 0 ? h->parent[h->spine] : -1;
    if (ok) {
        float sum = 0.0f; int k = 0;
        for (int e : h->eyes) if (e >= 0) { sum += h->bind[e].m[13]; ++k; }
        h->eyeY = k ? sum / k : h->bind[h->head].m[13] + 0.25f;
        if (h->pelvis >= 0) h->pelvisY = h->bind[h->pelvis].m[13];
        if (!(h->eyeY > 3.0f && h->eyeY < 12.0f)) h->eyeY = 0.0f;   // not a standing body: no height from it
        // [body] crouch: the legs, by the rig's own hierarchy and bind pose, not by class (bearstonem/d2r-pcvr's way, MIT,
        // vr/skeletons.cpp Learn): a bone named foot or ankle (not a toe, not an _end marker, not above the pelvis), its parent
        // the knee and that one's the hip, the hip under the pelvis, each joint lower than the one above it and the foot in the
        // lower half of the standing pelvis's height. Its side by where the foot stands (model +x is the hero's left).
        for (int foot = 0; foot < count; ++foot) {
            std::string lo = names[foot];
            for (char& ch : lo) ch = (char)tolower((unsigned char)ch);
            if ((lo.find("foot") == std::string::npos && lo.find("ankle") == std::string::npos) || lo.find("end") != std::string::npos ||
                lo.find("toe") != std::string::npos || h->upper[foot])
                continue;
            const int knee = h->parent[foot], hip = knee >= 0 ? h->parent[knee] : -1;
            if (hip < 0 || h->pelvis < 0 || hip == h->pelvis) continue;
            int up = hip;
            while (up >= 0 && up != h->pelvis) up = h->parent[up];
            if (up != h->pelvis) continue;
            const V3 a = Pos(h->bind[hip]), b = Pos(h->bind[knee]), c = Pos(h->bind[foot]);
            if (a.y - b.y < 0.1f || b.y - c.y < 0.1f || c.y > h->pelvisY * 0.5f) continue;
            const int side = c.x < 0.0f ? 0 : 1;
            if (h->leg[side][2] < 0) { h->leg[side][0] = hip; h->leg[side][1] = knee; h->leg[side][2] = foot; }
        }
        // ... a side the names gave none: by the rig's hierarchy and bind pose alone (vr/leg_chain.h - the barbarian's rig names its bones otherwise)
        if ((h->leg[0][2] < 0 || h->leg[1][2] < 0) && h->pelvis >= 0 && h->pelvisY > 0.5f) {
            std::vector<float> bx(count), by(count);
            for (int i = 0; i < count; ++i) { bx[i] = h->bind[i].m[12]; by[i] = h->bind[i].m[13]; }
            legchain::Leg legs[2];
            legchain::Find(count, h->parent.data(), bx.data(), by.data(), h->upper.data(), h->pelvis, h->pelvisY, legs);
            for (int side = 0; side < 2; ++side)
                if (h->leg[side][2] < 0 && legs[side].foot >= 0) { h->leg[side][0] = legs[side].hip; h->leg[side][1] = legs[side].knee; h->leg[side][2] = legs[side].foot; }
            if (h->leg[0][2] >= 0 && h->leg[1][2] >= 0 && (legs[0].foot >= 0 || legs[1].foot >= 0))
                NoteDoll("hero rig %s: its legs found by its bones' places, not their names (%s - %s - %s, %s - %s - %s)", h->cls >= 0 ? kHeroClasses[h->cls] : "?",
                         names[h->leg[0][0]].c_str(), names[h->leg[0][1]].c_str(), names[h->leg[0][2]].c_str(), names[h->leg[1][0]].c_str(), names[h->leg[1][1]].c_str(),
                         names[h->leg[1][2]].c_str());
        }
    }
    return true;
}

// Re-derive every bone after `from` that descends from a bone in `turned`
// (flags per bone), keeping each local transform from the animation's pose.
void Propagate(const Hero& h, const std::vector<Mat>& oldW, std::vector<Mat>& newW, std::vector<char>& turned, int from) {
    for (int i = from + 1; i < h.count; ++i) {
        const int p = h.parent[i];
        if (p < 0 || !turned[p]) continue;
        newW[i] = Mul(Mul(oldW[i], InvAffine(oldW[p])), newW[p]);
        turned[i] = 1;
    }
}

V3 Add(V3 a, V3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
V3 Scale(V3 a, float k) { return {a.x*k, a.y*k, a.z*k}; }
// A point through a row-vector matrix.
V3 Through(const Mat& m, V3 v) {
    return {v.x * m.m[0] + v.y * m.m[4] + v.z * m.m[8] + m.m[12],
            v.x * m.m[1] + v.y * m.m[5] + v.z * m.m[9] + m.m[13],
            v.x * m.m[2] + v.y * m.m[6] + v.z * m.m[10] + m.m[14]};
}

// Two-bone IK: shoulder S, lengths L1 L2, target T (pulled into reach), pole hint.
#if D2RVR_FIRST_PERSON
V3 Elbow(V3 S, float L1, float L2, V3& T, V3 pole) {
    V3 st = Sub(T, S);
    float d = Len(st);
    const float maxD = (L1 + L2) * 0.999f, minD = fabsf(L1 - L2) * 1.001f + 1e-4f;
    if (d > maxD) { T = Add(S, Scale(st, maxD / d)); st = Sub(T, S); d = maxD; }
    if (d < minD) { T = Add(S, Scale(Norm(st), minD)); st = Sub(T, S); d = minD; }
    const V3 dir = Scale(st, 1.0f / d);
    const float a = (L1 * L1 - L2 * L2 + d * d) / (2 * d);
    const float hgt = sqrtf(std::max(0.0f, L1 * L1 - a * a));
    const V3 p = Norm(Sub(pole, Scale(dir, Dot(pole, dir))));
    return Add(Add(S, Scale(dir, a)), Scale(p, hgt));
}

// Aims bone u at elbow E and bone l at target T, re-deriving both subtrees.
void AimChain(const Hero& h, int u, int l, int k, V3 E, V3 T, const std::vector<Mat>& oldW, std::vector<Mat>& newW) {
    const int n = h.count;
    TurnAboutSelf(newW[u], RotBetween(Sub(Pos(newW[l]), Pos(newW[u])), Sub(E, Pos(newW[u]))));
    std::vector<char> fromU(n, 0); fromU[u] = 1;
    Propagate(h, oldW, newW, fromU, u);
    TurnAboutSelf(newW[l], RotBetween(Sub(Pos(newW[k]), Pos(newW[l])), Sub(T, Pos(newW[l]))));
    std::vector<char> fromL(n, 0); fromL[l] = 1;
    Propagate(h, oldW, newW, fromL, l);
}

// Quaternions, x y z w.
struct Q4 { float x, y, z, w; };
Q4 QMul(Q4 a, Q4 b) {
    return {a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}
Q4 QAxis(float ax, float ay, float az, float deg) {
    const float h = deg * 0.00872664626f, s = sinf(h);
    return {ax * s, ay * s, az * s, cosf(h)};
}
V3 QRot(Q4 q, V3 v) {   // q v q*
    const V3 u = {q.x, q.y, q.z};
    const V3 t = Scale(Cross(u, v), 2.0f);
    return Add(Add(v, Scale(t, q.w)), Cross(u, t));
}
// The rotation whose columns are a0 a1 a2 (the images of x y z: QRot(q, e_i) = a_i; orthonormal, right-handed), as a quaternion.
Q4 QFromAxes(V3 a0, V3 a1, V3 a2) {
    float q[4];
    weaponback::QuatFromAxes({a0.x, a0.y, a0.z}, {a1.x, a1.y, a1.z}, {a2.x, a2.y, a2.z}, q);
    return {q[0], q[1], q[2], q[3]};
}

// A direction in the head's turn-only frame (x right, y up, z back) -> model
// space (its right is -x, ahead is +z), turned so "ahead of me" is where the
// camera looks: by the camera's yaw in model space (yaw = atan2(x, z)).
V3 ToModel(const float* v, float c, float s) {
    const V3 m = {-v[0], v[1], -v[2]};
    return {m.x * c + m.z * s, m.y, -m.x * s + m.z * c};
}

// v' = v * R for the 3x3 part only.
V3 RotOnly(V3 v, const Mat& R) {
    return {v.x*R.m[0] + v.y*R.m[4] + v.z*R.m[8], v.x*R.m[1] + v.y*R.m[5] + v.z*R.m[9], v.x*R.m[2] + v.y*R.m[6] + v.z*R.m[10]};
}

// The hand turned like the controller. The bone's own axes are unknown (and
// mirrored on one side), so they are read off the knuckles: wrist -> middle
// knuckle is where the hand points, pinky -> index knuckle runs along the fist.
// Held in a fist, that line is the grip's -Z (pinky to thumb side). The grip's
// +X goes into the right palm / out of the left one, both to the right in a
// natural hold, so Y = Z x X makes the pointing -Y: a vertical fist punching
// forward has -Z up, X right, Y back. (+Y turned the hand round the hilt by
// 180 degrees, knuckles at the face.) Children keep their local offsets, so the two
// directions are fixed in the hand bone's frame whatever the animation does.
struct Wrist { bool ok = false; V3 pLocal, kLocal; float det = 1.0f; V3 point, along; float reach = 0.0f; };

Wrist PlanWrist(const Hero& h, int side, const std::vector<Mat>& w, const Input& in, float c, float s) {
    Wrist r;
    const int k = h.hand[side], i = h.ind[side], m = h.mid[side], p = h.pnk[side];
    if (!in.wrist || i < 0 || m < 0 || p < 0) return r;
    const Mat inv = InvAffine(w[k]);
    const V3 point = Sub(Pos(w[m]), Pos(w[k])), along = Sub(Pos(w[i]), Pos(w[p]));
    if (Len(point) < 1e-4f || Len(along) < 1e-4f) return r;
    r.pLocal = Norm(RotOnly(point, inv));
    r.kLocal = Norm(RotOnly(along, inv));
    r.reach = Len(point);
    const float* q = w[k].m;
    r.det = q[0]*(q[5]*q[10] - q[6]*q[9]) - q[1]*(q[4]*q[10] - q[6]*q[8]) + q[2]*(q[4]*q[9] - q[5]*q[8]) < 0 ? -1.0f : 1.0f;
    // the controller, plus the ini offsets in its own axes (yaw and roll mirror on the left)
    const float* cq = in.rot[side];
    Q4 qc = {cq[0], cq[1], cq[2], cq[3]};
    const float ql = sqrtf(qc.x*qc.x + qc.y*qc.y + qc.z*qc.z + qc.w*qc.w);
    if (!(ql > 0.5f)) return r;
    qc = {qc.x/ql, qc.y/ql, qc.z/ql, qc.w/ql};
    const float mir = side == 0 ? 1.0f : -1.0f;
    qc = QMul(qc, QMul(QAxis(1, 0, 0, in.wristDeg[0]), QMul(QAxis(0, 1, 0, mir * in.wristDeg[1]), QAxis(0, 0, 1, mir * in.wristDeg[2]))));
    const V3 py = QRot(qc, {0, -1, 0}), pz = QRot(qc, {0, 0, -1});
    const float a[3] = {py.x, py.y, py.z}, b[3] = {pz.x, pz.y, pz.z};
    r.point = Norm(ToModel(a, c, s));
    r.along = Norm(ToModel(b, c, s));
    r.ok = true;
    return r;
}

// Rotation part of the hand: pLocal -> point, kLocal -> along (made square to it).
void ApplyWrist(Mat& hand, const Wrist& r) {
    const V3 a1 = r.pLocal, a2 = Norm(Sub(r.kLocal, Scale(a1, Dot(r.kLocal, a1)))), a3 = Cross(a1, a2);
    const V3 b1 = r.point, b2 = Norm(Sub(r.along, Scale(b1, Dot(r.along, b1)))), b3 = Scale(Cross(b1, b2), r.det);
    const V3 A[3] = {a1, a2, a3}, B[3] = {b1, b2, b3};
    float R[9] = {};   // R = A^T B
    auto at = [](V3 v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; };
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        R[i*3+j] = at(A[0], i) * at(B[0], j) + at(A[1], i) * at(B[1], j) + at(A[2], i) * at(B[2], j);
    for (int i = 0; i < 3; ++i) {
        const float len = sqrtf(hand.m[i*4]*hand.m[i*4] + hand.m[i*4+1]*hand.m[i*4+1] + hand.m[i*4+2]*hand.m[i*4+2]);
        for (int j = 0; j < 3; ++j) hand.m[i*4+j] = R[i*3+j] * len;
    }
}

// Where the controller puts that hand's grip point (the middle of the palm), in
// model space. The hands hang off the eye we see through, so they land where
// the real ones are; the head bone sits elsewhere (camera height and side are
// set apart from it) and put them low and to one side.
V3 ControllerPoint(const Hero& h, int side, const Input& in, const std::vector<Mat>& newW, float c, float s) {
    const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
    // hero units per metre: the eye (or head) height in the model over the user's
    const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;
    const float hv[3] = {in.hand[side][0] + in.handOffset[0], in.hand[side][1] + in.handOffset[1], in.hand[side][2] + in.handOffset[2]};
    return Add(head, Scale(ToModel(hv, c, s), upm));
}

// The right controller held like a gun, in model space: F where it points, U its
// top, R its right. In the pose the bridge sends, held in a fist, the knuckles' -Y
// point ahead (as a bow aims, vrcam HandRay) and -Z is the top: with -Z taken for
// ahead the crossbow's stock stood straight up (seen with its axes drawn, 2026-10-05).
// The raw controller, without the [hands] wrist offsets.
bool GunFrame(const Input& in, float c, float s, V3* F, V3* U, V3* R, int side = 0) {   // side: the controller that holds it ([hands] xbow_handover)
    const float* cq = in.rot[side & 1];
    Q4 q = {cq[0], cq[1], cq[2], cq[3]};
    const float ql = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (!(ql > 0.5f)) return false;
    q = {q.x/ql, q.y/ql, q.z/ql, q.w/ql};
    const V3 f = QRot(q, {0, -1, 0}), u = QRot(q, {0, 0, -1}), r = QRot(q, {1, 0, 0});
    const float a[3] = {f.x, f.y, f.z}, b[3] = {u.x, u.y, u.z}, d[3] = {r.x, r.y, r.z};
    *F = Norm(ToModel(a, c, s)); *U = Norm(ToModel(b, c, s)); *R = Norm(ToModel(d, c, s));
    return true;
}

// [hands] xbow_handover: which controller holds the crossbow (0 right - the home - or 1 left), for this pose. The hand that does not hold it
// takes it with a new squeeze of its grip at the holding palm - within kHandoverReachM of it and nearer it than the support hand's place on
// the fore-grip (xbowgrip::HandoverNear; 0.170: a 12 cm reach between the two controllers' grip points never fired) - and the other lets go
// (xbowgrip::Handover). Back to the right hand on another weapon or hero model, or with the tick off. Visual and aim only.
constexpr float kHandoverReachM = 0.25f;
std::atomic<int> g_xbowHolder{0};
V3 g_xbowSupportAt{};            // the support hand's place on the crossbow last pose (model space), for the hand-over's "nearer the grip"
uint64_t g_xbowSupportKey = 0;   // whose: hero model, weapon, holder (0 = none)
int XbowHolderStep(const Hero& h, const Input& in, const std::vector<Mat>& newW, float c, float s) {
    static xbowgrip::Handover m;
    static uint64_t key = ~0ull;
    const uint64_t k = ((uint64_t)HeroGen() << 32) | (uint32_t)in.weaponType;
    if (!in.gunStock || !in.xbowHandover || in.mode != 2) {
        if (m.holder != 0) XbowNote("crossbow hand-over: back in the right hand (the tick off, or no crossbow held the gun's way)");
        m.Reset(0); key = k; g_xbowHolder.store(0);
        return 0;
    }
    if (k != key) {
        if (m.holder != 0) XbowNote("crossbow hand-over: another weapon or hero model - back in the right hand");
        key = k; m.Reset(0);
    }
    const bool tracked[2] = {(in.handsValid & 1u) != 0, (in.handsValid & 2u) != 0};
    float nearM = INFINITY, gripM = NAN, supportM = -1.0f;
    if (tracked[0] && tracked[1] && h.head >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
        const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
        const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
        const int taker = m.holder ^ 1;
        const V3 t = ControllerPoint(h, taker, in, newW, c, s);
        gripM = Len(Sub(ControllerPoint(h, m.holder, in, newW, c, s), t)) / upm;
        if (g_xbowSupportKey == (k ^ ((uint64_t)m.holder << 63)) && g_xbowSupportKey != 0) supportM = Len(Sub(g_xbowSupportAt, t)) / upm;
        nearM = xbowgrip::HandoverNear(gripM, supportM, kHandoverReachM);
    }
    const float g[2] = {in.grip[0], in.grip[1]};
    const bool supportHeld = g_xbowSupportHeld && g_xbowSupportHeldKey == (k ^ ((uint64_t)m.holder << 63));
    {   // every squeeze of the free hand, for the log: how far from the grip and from the fore-grip, and whether it takes the crossbow
        static bool was = false;
        const int taker = m.holder ^ 1;
        const bool now = tracked[taker] && g[taker] > 0.6f;
        if (now && !was) {
            char t[220];
            snprintf(t, sizeof t, "crossbow hand-over: the %s hand squeezed %.0f cm from the crossbow's grip, %s - %s", taker ? "left" : "right",
                     gripM * 100.0f, supportM >= 0.0f ? (std::to_string((int)std::lround(supportM * 100.0f)) + " cm from the fore-grip").c_str() : "no fore-grip place yet",
                     std::isfinite(nearM) ? "near enough (taken unless the grip was still held from before)"
                                          : gripM > kHandoverReachM ? "too far from the grip (25 cm)" : "nearer the fore-grip: the support's squeeze");
            XbowNote(t);
        }
        was = now;
    }
    if (m.Step(g, tracked, nearM, kHandoverReachM, supportHeld))
        XbowNote(m.holder ? "crossbow hand-over: the left hand took it by its grip - it is held in the left hand, the right one is its support"
                          : "crossbow hand-over: the right hand took it by its grip - back in the right hand, the left one is its support");
    g_xbowHolder.store(m.holder);
    return m.holder;
}

// Row-vector form of the turn by deg about the unit axis k.
Mat TurnAbout(V3 k, float deg) {
    const float t = deg * 0.0174532925f, cs = cosf(t), sn = sinf(t), C = 1.0f - cs;
    Mat r{};
    r.m[0] = cs + k.x * k.x * C;       r.m[1] = k.y * k.x * C + k.z * sn; r.m[2] = k.z * k.x * C - k.y * sn;
    r.m[4] = k.x * k.y * C - k.z * sn; r.m[5] = cs + k.y * k.y * C;       r.m[6] = k.z * k.y * C + k.x * sn;
    r.m[8] = k.x * k.z * C + k.y * sn; r.m[9] = k.y * k.z * C - k.x * sn; r.m[10] = cs + k.z * k.z * C;
    r.m[15] = 1.0f;
    return r;
}

// An arm's bones (upper arm, forearm and all under them) scaled about `pivot` by `k`; the hand, its fingers and what it holds
// put back at their own size on the new wrist. The T-pose fit's and [arms] full_reach's stretch.
void ScaleArmAbout(const Hero& h, int side, V3 pivot, float k, std::vector<Mat>& newW) {
    const int n = h.count, u = h.upr[side], hk = h.hand[side];
    if (u < 0 || hk < 0) return;
    const std::vector<Mat> before = newW;
    std::vector<char> subtree(n, 0);
    subtree[u] = 1;
    for (int i = u; i < n; ++i) {
        if (i != u && h.parent[i] >= 0) subtree[i] = subtree[h.parent[i]];
        if (subtree[i]) bodycal::ScaleAbout(newW[i].m, {pivot.x, pivot.y, pivot.z}, k);
    }
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) newW[hk].m[row * 4 + col] = before[hk].m[row * 4 + col];   // the hand's own size and turn
    std::vector<char> fromHand(n, 0);
    fromHand[hk] = 1;
    Propagate(h, before, newW, fromHand, hk);   // fingers, weapon bones: their offsets from the hand as they were
}

// [arms] full_reach (vr/full_reach.h): set by PoseBody for the pose it is posing (the pose thread's), and what it did since the
// last note (ArmTakeNote), each arm's worst.
bool g_fullReachPose = false;
struct FullReachSeen { float shortPct = 0.0f, turnDeg = 0.0f, stretchPct = 0.0f; unsigned acted = 0; };
FullReachSeen g_fullReachSeen[2];
SRWLOCK g_armNoteLock = SRWLOCK_INIT;
std::string g_armNote;   // ArmTakeNote's lines (the arms as drawn, once per rig; full_reach's work)
ULONGLONG g_fullReachNoteAt = 0;
// Once a pose: full_reach's worst since the last line, a line every few seconds while it acts (a log line costs a millisecond).
void NoteFullReach() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_fullReachNoteAt < 4000 || (!g_fullReachSeen[0].acted && !g_fullReachSeen[1].acted)) return;
    g_fullReachNoteAt = now;
    std::string line = "full reach (the last few seconds):";
    for (int side = 0; side < 2; ++side) {
        FullReachSeen& f = g_fullReachSeen[side];
        if (!f.acted) continue;
        char b[200];
        snprintf(b, sizeof b, " the %s hand out of the arm's reach in %u poses, at most %.0f %% of the arm's length past it - the shoulder turned up to %.1f deg toward it, the arm lengthened up to %.0f %%;",
                 side ? "left" : "right", f.acted, f.shortPct, f.turnDeg, f.stretchPct);
        line += b;
        f = FullReachSeen{};
    }
    AcquireSRWLockExclusive(&g_armNoteLock);
    if (g_armNote.size() < 3000) g_armNote += (g_armNote.empty() ? "" : "\n") + line;
    ReleaseSRWLockExclusive(&g_armNoteLock);
}
// The upper arm's parent when it is this arm's own collarbone: under the chest, neither the head nor the other arm under it
// (a rig whose upper arm hangs straight off the spine has none: -1, no turn).
int Clavicle(const Hero& h, int side) {
    const int u = h.upr[side], p = u >= 0 ? h.parent[u] : -1;
    if (p < 0 || p == h.spine || p == h.neck || (int)h.upper.size() <= p || !h.upper[p] || h.inHead[p]) return -1;
    for (const int o : {h.upr[side ^ 1], h.head})
        for (int i = o; i >= 0; i = h.parent[i])
            if (i == p) return -1;
    return p;
}

// One arm's two-bone IK, the wrist to T: elbow down, a little out to its own
// side and back - of the body as turned.
void Reach(const Hero& h, int side, V3 T, bool turn, float c, float s, const std::vector<Mat>& baseW, std::vector<Mat>& newW) {
    const int u = h.upr[side], l = h.lwr[side], k = h.hand[side];
    // [arms] full_reach: a target past the arm's reach brings the shoulder along (the collarbone turned about its joint, the least
    // turn, bounded), and what is still missing lengthens the arm - the hand on its controller, never short of it
    const bool full = g_fullReachPose;
    const V3 want = T;
    float shortPct = 0.0f, turnDeg = 0.0f;
    if (full) {
        const int p = Clavicle(h, side);
        // a Reach already run on this arm in this pose (the bow's, the grip fit's) may have turned or lengthened it: from the arm as it
        // was before any, so they do not pile up (no turn or stretch before: as it is - the IK re-aims the chain anyway)
        const int top = p >= 0 ? p : u;
        const float rowNow = Len({newW[u].m[0], newW[u].m[1], newW[u].m[2]}), rowBase = Len({baseW[u].m[0], baseW[u].m[1], baseW[u].m[2]});
        if ((p >= 0 && memcmp(newW[p].m, baseW[p].m, sizeof newW[p].m) != 0) || std::fabs(rowNow - rowBase) > 1e-4f * std::max(1e-3f, rowBase)) {
            newW[top] = baseW[top];
            std::vector<char> fromTop(h.count, 0);
            fromTop[top] = 1;
            Propagate(h, baseW, newW, fromTop, top);
        }
        const V3 S0 = Pos(newW[u]);
        const float L = Len(Sub(Pos(newW[l]), S0)) + Len(Sub(Pos(newW[k]), Pos(newW[l])));
        if (L > 1e-5f) shortPct = std::max(0.0f, Len(Sub(want, S0)) / (L * 0.999f) - 1.0f) * 100.0f;
        if (p >= 0 && shortPct > 0.0f) {
            const V3 P = Pos(newW[p]);
            const fullreach::Turned t = fullreach::TurnShoulder({P.x, P.y, P.z}, {S0.x, S0.y, S0.z}, {want.x, want.y, want.z}, L);
            if (t.turn) {
                turnDeg = t.rad * 57.2957795f;
                TurnAboutSelf(newW[p], TurnAbout({t.axis.x, t.axis.y, t.axis.z}, turnDeg));
                std::vector<char> fromP(h.count, 0);
                fromP[p] = 1;
                Propagate(h, baseW, newW, fromP, p);
            }
        }
    }
    const V3 S = Pos(newW[u]);
    const float L1 = Len(Sub(Pos(newW[l]), S)), L2 = Len(Sub(Pos(newW[k]), Pos(newW[l])));
    V3 pole = {side == 0 ? -0.5f : 0.5f, -1.0f, -0.3f};
    if (turn) pole = {pole.x * c + pole.z * s, pole.y, -pole.x * s + pole.z * c};
    const V3 E = Elbow(S, L1, L2, T, pole);
    AimChain(h, u, l, k, E, T, baseW, newW);
    if (full) {
        const float stretch = fullreach::Stretch(Len(Sub(want, S)), Len(Sub(Pos(newW[k]), S)));
        if (stretch > 1.0f) ScaleArmAbout(h, side, S, stretch, newW);
        if (shortPct > 0.0f) {
            FullReachSeen& seen = g_fullReachSeen[side & 1];
            seen.shortPct = std::max(seen.shortPct, shortPct);
            seen.turnDeg = std::max(seen.turnDeg, turnDeg);
            seen.stretchPct = std::max(seen.stretchPct, (stretch - 1.0f) * 100.0f);
            ++seen.acted;
        }
    }
}

// [hands] forearm_twist (vr/forearm_twist.h): Reach aims the forearm by the smallest turn from the animation's, so it kept the
// animation's roll and the controller's whole roll landed on the hand - the wrist screwed round on a still arm. Here the hand's
// roll about the forearm's line, against how the bind pose holds the hand on the forearm, is shared: the forearm bone turns
// `share` of it about its own line through the elbow (the wrist, on that line, stays), a twist bone along it more the nearer the
// wrist; the hand and all under it stay as the controller put them. Returns the roll read (degrees, continuous), 0 for none.
float g_twistLast[2] = {NAN, NAN};   // the last frame's roll, per side (the pose thread's)
std::atomic<float> g_twistOut[2];    // for the log
void Rows3(const Mat& m, float out[9]) {
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out[r * 3 + c] = m.m[r * 4 + c];
}
float TwistForearm(const Hero& h, int side, float share, std::vector<Mat>& newW) {
    const int n = h.count, l = h.lwr[side], k = h.hand[side];
    const V3 E = Pos(newW[l]), W = Pos(newW[k]);
    if (!(Len(Sub(W, E)) > 1e-4f)) { g_twistLast[side] = NAN; return 0.0f; }
    const V3 a = Norm(Sub(W, E));
    const Mat ref = Mul(Mul(h.bind[k], InvAffine(h.bind[l])), newW[l]);   // the hand as the bind pose holds it on this forearm
    float r9[9], n9[9];
    Rows3(ref, r9);
    Rows3(newW[k], n9);
    const float axis[3] = {a.x, a.y, a.z};
    const float roll = forearmtwist::Unwrap(g_twistLast[side], forearmtwist::TwistDeg(r9, n9, axis));
    if (!std::isfinite(roll)) return 0.0f;
    g_twistLast[side] = roll;
    g_twistOut[side].store(roll);
    const std::vector<Mat> before = newW;
    const float base = forearmtwist::Share(share, 0.0f) * roll;
    TurnAboutSelf(newW[l], TurnAbout(a, base));
    std::vector<char> fromL(n, 0);
    fromL[l] = 1;
    Propagate(h, before, newW, fromL, l);
    for (const Hero::Twist& t : h.twist[side]) {
        const float extra = forearmtwist::Share(share, t.t) * roll - base;
        if (std::fabs(extra) < 0.01f) continue;
        const std::vector<Mat> was = newW;
        TurnAboutSelf(newW[t.bone], TurnAbout(a, extra));
        std::vector<char> fromT(n, 0);
        fromT[t.bone] = 1;
        Propagate(h, was, newW, fromT, t.bone);
    }
    std::vector<char> inK(n, 0);   // the hand and everything under it: the controller's, as before
    inK[k] = 1;
    newW[k] = before[k];
    for (int i = k + 1; i < n; ++i)
        if (h.parent[i] >= 0 && inK[h.parent[i]]) { inK[i] = 1; newW[i] = before[i]; }
    return roll;
}

// [shield] forearm: the shield strapped to the left forearm, not hung on the hand. Its attach bone is set from the rig's own bind
// pose of it against the forearm (bearstonem/d2r-pcvr's shield follow takes the same bind relationship, to the wrist), turned
// with the forearm's twist where it sits ([hands] forearm_twist: the forearm bone's share and more toward the wrist) - no game
// animation, no Weapon Adjust of the other hand's weapon - then [shield] x y z (cm) and pitch yaw roll (deg) in its own axes.
// A shield the game hides (its attach bone shrunk to a point: a weapon swap) stays the game's. True when it was set.
// [hands] shoulder_draw (vr/shield_arm.h): `side` is the arm it is strapped to, 1 the left (as above), 0 the right - the same shield reflected in the body's plane and carried
// by the right forearm the same way (shieldmirror::RightArmMatrix), the handedness of the left attach bone kept, the user's corrections mirrored.
bool StrapShield(const Hero& h, const Input& in, const std::vector<Mat>& oldW, std::vector<Mat>& newW, float roll, int side = 1, int bone = -1) {
    const int n = h.count, wb = bone >= 0 ? bone : h.weapon[1], l = h.lwr[side], k = h.hand[side];   // (bone: the game's left attach bone, or shield_attach - the same strap for either)
    const int gameHand = h.hand[1];   // the game hangs the shield on the left wrist: whether it hides it is read there
    if (wb < 0 || l < 0 || k < 0 || gameHand < 0 || !h.bindOk) return false;
    const Mat anim = Mul(oldW[wb], InvAffine(oldW[gameHand]));
    if (Len({anim.m[0], anim.m[1], anim.m[2]}) < 0.3f) return false;   // hidden by the game
    Mat local{};
    shieldmirror::Axes axes;
    float planeX = 0.0f;
    V3 bindShield = Pos(h.bind[wb]);   // where the shield stands in the bind pose, against the forearm it is strapped to (reflected for the right one)
    if (side == 1) {
        local = Mul(h.bind[wb], InvAffine(h.bind[l]));
        const float size = Len({local.m[0], local.m[1], local.m[2]});
        if (!(size > 0.3f && size < 3.0f)) return false;
    } else {
        if (h.lwr[1] < 0) return false;
        weaponback::Mat bl, br, bs;
        memcpy(bl.m, h.bind[h.lwr[1]].m, sizeof bl.m); memcpy(br.m, h.bind[l].m, sizeof br.m); memcpy(bs.m, h.bind[wb].m, sizeof bs.m);
        planeX = shieldmirror::PlaneX(bl, br);
        const float size = Len({h.bind[wb].m[0], h.bind[wb].m[1], h.bind[wb].m[2]});
        if (!(size > 0.3f && size < 3.0f)) return false;
        const V3 pe = Pos(h.bind[h.lwr[1]]), pw = Pos(h.bind[h.hand[1] >= 0 ? h.hand[1] : k]);
        axes = shieldmirror::FaceAxes(bs, {pe.x, pe.y, pe.z}, {pw.x, pw.y, pw.z});
        bindShield.x = 2.0f * planeX - bindShield.x;
    }
    Mat arm = newW[l];
    const V3 E = Pos(newW[l]), W = Pos(newW[k]);
    if (roll != 0.0f && in.forearmTwist >= 0.0f && Len(Sub(W, E)) > 1e-4f) {
        const V3 bE = Pos(h.bind[l]), bEW = Sub(Pos(h.bind[k]), bE);
        const float ll = Dot(bEW, bEW);
        const float t = ll > 1e-8f ? std::clamp(Dot(Sub(bindShield, bE), bEW) / ll, 0.0f, 1.0f) : 1.0f;
        TurnAboutSelf(arm, TurnAbout(Norm(Sub(W, E)), (forearmtwist::Share(in.forearmTwist, t) - forearmtwist::Share(in.forearmTwist, 0.0f)) * roll));
    }
    const std::vector<Mat> before = newW;
    if (side == 1) {
        newW[wb] = Mul(local, arm);
    } else {
        weaponback::Mat bs, br, an, out;
        memcpy(bs.m, h.bind[wb].m, sizeof bs.m); memcpy(br.m, h.bind[l].m, sizeof br.m); memcpy(an.m, arm.m, sizeof an.m);
        const double d = boneguard::Det(oldW[wb].m);
        const int want = d < -1e-9 ? -1 : d > 1e-9 ? 1 : -1;   // the left attach bone's own handedness (a mirror)
        if (!shieldmirror::RightArmMatrix(bs, br, an, planeX, axes.acrossRow, want, &out)) return false;
        memcpy(newW[wb].m, out.m, sizeof newW[wb].m);
    }
    float mirrored[6];
    shieldmirror::MirrorAdjust(in.shieldAdj, axes.acrossRow, mirrored);
    const float* a = side == 1 ? in.shieldAdj : mirrored;
    const Mat plain = newW[wb];   // the strap without the user's corrections (for ShieldAdjNote)
    float moveCm = 0.0f, turnDeg = 0.0f;
    if (a[0] != 0.0f || a[1] != 0.0f || a[2] != 0.0f || a[3] != 0.0f || a[4] != 0.0f || a[5] != 0.0f) {   // as Weapon Adjust moves a weapon
        const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head >= 0 ? h.head : 0]).y;
        const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
        auto turn = [](int axis, float deg) {   // row-vector rotation about one of the bone's own axes
            Mat r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
            const float t = deg * 0.0174532925f, c = cosf(t), s = sinf(t);
            const int i = (axis + 1) % 3, j = (axis + 2) % 3;
            r.m[i * 4 + i] = c; r.m[i * 4 + j] = s; r.m[j * 4 + i] = -s; r.m[j * 4 + j] = c;
            return r;
        };
        V3 shift{0, 0, 0};
        for (int r = 0; r < 3; ++r) {
            const V3 ax = Norm(V3{newW[wb].m[r * 4], newW[wb].m[r * 4 + 1], newW[wb].m[r * 4 + 2]});
            shift = Add(shift, Scale(ax, a[r] * 0.01f * upm));
        }
        const V3 p = Pos(newW[wb]);
        newW[wb] = Mul(Mul(Mul(turn(0, a[3]), turn(1, a[4])), turn(2, a[5])), newW[wb]);   // turned in its own frame, about its own origin
        newW[wb].m[12] = p.x + shift.x; newW[wb].m[13] = p.y + shift.y; newW[wb].m[14] = p.z + shift.z;
        moveCm = Len(Sub(Pos(newW[wb]), Pos(plain))) / upm * 100.0f;
        float tr = 0.0f;   // the angle between the two orientations (the rows are orthogonal): cos = (trace - 1) / 2
        for (int r = 0; r < 3; ++r) tr += Dot(Norm(V3{plain.m[r * 4], plain.m[r * 4 + 1], plain.m[r * 4 + 2]}), Norm(V3{newW[wb].m[r * 4], newW[wb].m[r * 4 + 1], newW[wb].m[r * 4 + 2]}));
        turnDeg = acosf(std::clamp((tr - 1.0f) * 0.5f, -1.0f, 1.0f)) * 57.29578f;
    }
    for (int q = 0; q < 6; ++q) g_shieldAdjSeen[q].store(a[q]);
    g_shieldAdjMoveCm.store(moveCm);
    g_shieldAdjTurnDeg.store(turnDeg);
    std::vector<char> fromS(n, 0);
    fromS[wb] = 1;
    Propagate(h, before, newW, fromS, wb);
    return true;
}

// a's rotation part turned toward b's by t (rows blended, made square again,
// each keeping a's length and handedness); a's position.
Mat BlendRot(const Mat& a, const Mat& b, float t) {
    V3 r[3]; float len[3];
    for (int i = 0; i < 3; ++i) {
        const V3 ra = {a.m[i*4], a.m[i*4+1], a.m[i*4+2]}, rb = {b.m[i*4], b.m[i*4+1], b.m[i*4+2]};
        len[i] = Len(ra);
        r[i] = Add(Scale(Norm(ra), 1.0f - t), Scale(Norm(rb), t));
    }
    const float* q = a.m;
    const float det = q[0]*(q[5]*q[10] - q[6]*q[9]) - q[1]*(q[4]*q[10] - q[6]*q[8]) + q[2]*(q[4]*q[9] - q[5]*q[8]);
    r[0] = Norm(r[0]);
    r[1] = Norm(Sub(r[1], Scale(r[0], Dot(r[1], r[0]))));
    r[2] = Scale(Cross(r[0], r[1]), det < 0 ? -1.0f : 1.0f);
    Mat m = a;
    for (int i = 0; i < 3; ++i) { m.m[i*4] = r[i].x * len[i]; m.m[i*4+1] = r[i].y * len[i]; m.m[i*4+2] = r[i].z * len[i]; }
    return m;
}

// staff_free_left: the left hand on the staff or off it. The grip goes on
// above 0.6 and off below 0.4 (no flicker at the threshold), and the hand
// moves between the two poses over kStaffEaseS - no jump either way.
constexpr float kStaffEaseS = 0.1f;
constexpr float kStaffReach = 1.5f;   // the left hand slides up to this many of the game's hand gaps from the right one
struct StaffHold {
    bool pressed = false;   // the grip, as a button (with the two thresholds)
    bool on = false;        // the hand holds the staff: pressed while near it, and not let go since
    float t = 0.0f;
    LONGLONG last = 0;
    // for vrcam's aim
    float dir[3] = {};
    float gap = 0.0f;
    ULONGLONG at = 0;
};
SRWLOCK g_staffLock = SRWLOCK_INIT;
StaffHold g_staff;
// [hands] xbow_grip_latch: the crossbow's own grip latch (vr/xbow_grip.h), under g_staffLock - the staves' StaffEase is not it.
xbowgrip::Latch g_xbowLatch;

// The hero's forearms as drawn, for FlatVR (the toolbar lies on one): the
// elbow (forearm bone) and the wrist (hand bone), and the way the palm faces,
// square to the arm - it turns as the forearm twists, not as the wrist bends. In BodyWalk's hand
// frame: metres from the head, x right, y up, z back, head yaw only (the
// frame the controllers came in by), so FlatVR lays them back where they were
// drawn. [0] right, [1] left.
// palm: the palm's middle (wrist to middle knuckle, halfway, 3 cm out of the palm) - what a belt potion is held by
struct ForearmOut { float elbow[3], wrist[3], across[3], palm[3], palmModel[3]; bool palmOk; ULONGLONG at;   // palmModel: in his model space
                    float elbowW[3], wristW[3], acrossW[3], pointW[3], faceW[3]; bool worldOk, handW; ULONGLONG worldAt;   // and in the WORLD (HeroArmWorld), made at the pose at worldAt
                    bool boxOk; float boxCW[3], boxHW[3][3]; };   // [hud] hand_vitals_orb_on_bone: the forearm bone's mesh box in the world, at the end of the pose
// [hud] hand_vitals_orb_on_bone: each forearm bone's box off the hero's own meshes (the pose thread's), read again when his parts change (another armour)
struct ArmBoxRead {
    uintptr_t skel = 0;
    uint64_t key = 0;
    ULONGLONG lookAt = 0;
    bool ok[2] = {};
    float mn[2][3] = {}, mx[2][3] = {};   // in the forearm bone's own frame, the model's units
};
ArmBoxRead g_armBoxes;
std::atomic<const char*> g_armWorldWhy{"not yet posed"};   // why the last pose could not put the arm in the world (HeroArmWhy); "" while it could

SRWLOCK g_forearmLock = SRWLOCK_INIT;
ForearmOut g_forearm[2] = {};
// [hands] spell_aim_model (CastPoseNow): each hand and the weapon's attach bone as the finished pose draws them, head frame, metres from the eye
struct CastHandOut { float palm[3], fingers[3], face[3]; ULONGLONG at; };
struct CastWeaponOut { int side; float origin[3], tip[3]; ULONGLONG at; };
SRWLOCK g_castLock = SRWLOCK_INIT;
CastHandOut g_castHand[2] = {};
CastWeaponOut g_castWeapon = {};

// ToModel's inverse: a model-space vector to the hand frame.
void FromModel(V3 o, float c, float s, float out[3]) {
    const float mx = o.x * c - o.z * s, mz = o.x * s + o.z * c;
    out[0] = -mx; out[1] = o.y; out[2] = -mz;
}

// inReach: the left hand is close enough to the staff to take it. A grip pressed
// far from it takes nothing until it is let go and pressed again near it. busy: the
// hand holds something else (a belt potion): it lets go of the staff and takes it only
// with a new press once the hand is empty ("a hand with a bottle from the belt does not
// take the staff or a two-handed weapon", 2026-10-13).
float StaffEase(float grip, bool inReach, bool busy) {
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&g_staffLock);
    const float dt = g_staff.last ? std::clamp((float)((double)(now.QuadPart - g_staff.last) / (double)f.QuadPart), 0.0f, kStaffEaseS) : kStaffEaseS;
    g_staff.last = now.QuadPart;
    const bool pressed = grip < 0.0f || grip > 0.6f ? true : grip < 0.4f ? false : g_staff.pressed;
    if (busy) g_staff.on = false;
    else if (grip < 0.0f) g_staff.on = true;   // no grip from the bridge: always on the staff
    else if (pressed && !g_staff.pressed) g_staff.on = inReach;
    else if (!pressed) g_staff.on = false;
    g_staff.pressed = pressed;
    g_staff.t = std::clamp(g_staff.t + (g_staff.on ? dt : -dt) / kStaffEaseS, 0.0f, 1.0f);
    const float t = g_staff.t;
    ReleaseSRWLockExclusive(&g_staffLock);
    return t * t * (3.0f - 2.0f * t);
}

// [hands] xbow_grip_latch: StaffEase for a crossbow, by its own latch (xbowgrip::Latch): held while the grip is, let go at once
// when the weapon or the hero's model changes (no easing from the old weapon's place), on lost tracking and with no grip from
// the bridge (the hand free - before, it jumped onto the stock); a squeeze from before (the shoulder swap's) takes nothing.
float XbowEase(float grip, bool inReach, bool busy, bool tracked, uint64_t hero, uint32_t weapon) {
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&g_staffLock);
    const float dt = g_staff.last ? std::clamp((float)((double)(now.QuadPart - g_staff.last) / (double)f.QuadPart), 0.0f, kStaffEaseS) : kStaffEaseS;
    g_staff.last = now.QuadPart;
    if (!g_xbowLatch.Current(hero, weapon)) g_staff.t = 0.0f;
    g_staff.on = g_xbowLatch.Step(hero, weapon, tracked, grip, inReach, busy);
    g_staff.pressed = g_xbowLatch.pressed;
    g_staff.t = std::clamp(g_staff.t + (g_staff.on ? dt : -dt) / kStaffEaseS, 0.0f, 1.0f);
    const float t = g_staff.t;
    ReleaseSRWLockExclusive(&g_staffLock);
    return t * t * (3.0f - 2.0f * t);
}
void XbowLatchReset() {
    AcquireSRWLockExclusive(&g_staffLock);
    if (g_xbowLatch.known) { g_xbowLatch.Reset(); g_staff.on = g_staff.pressed = false; g_staff.t = 0.0f; }
    ReleaseSRWLockExclusive(&g_staffLock);
}

// The palm's middle (wrist to middle knuckle, halfway) in the hand bone's own frame: from the rig's bind pose - the hand's own bones,
// whatever its fingers do this frame (xbowgrip::PalmInHand) - else from `pose` as it stands. One definition for every hand put on a
// shaft by its palm here (the crossbow's support hand, a sword's hilt, a spear's or a staff's shaft laid by its bone's axis).
V3 PalmInHandOf(const Hero& h, int side, const std::vector<Mat>& pose) {
    const int k = h.hand[side], m = h.mid[side];
    if (k < 0 || m < 0) return {0.0f, 0.0f, 0.0f};
    if (h.bindOk && k < (int)h.bind.size() && m < (int)h.bind.size()) {
        xbowgrip::M hb, kb;
        memcpy(hb.m, h.bind[k].m, sizeof hb.m); memcpy(kb.m, h.bind[m].m, sizeof kb.m);
        xbowgrip::V p;
        if (xbowgrip::PalmInHand(hb, kb, &p)) return {p.x, p.y, p.z};
    }
    return Through(InvAffine(pose[k]), Scale(Add(Pos(pose[k]), Pos(pose[m])), 0.5f));
}

// [hands] fist: a free hand's fingers from the bind pose (an open hand), each
// joint bent toward the palm by the grip - a little with it let go (relaxed),
// a fist with it squeezed. The bone axes are unknown and differ per rig, so
// the bend is worked out from where the knuckles are: wrist -> middle knuckle
// points, pinky -> index knuckle runs across, and the palm faces their cross
// product - away from it on the right hand, toward it on the left (model
// space: x the hero's left, y up, z ahead; a vertical fist thumb up, the
// right palm faces +x). Each finger bends about one axis, square to its first
// segment and the palm, joint after joint from the knuckle out.
constexpr float kFingerFistDeg[3] = {80.0f, 95.0f, 65.0f};   // knuckle, middle, tip
constexpr float kThumbFistDeg[3] = {25.0f, 35.0f, 40.0f};

// curl (optional): how bent the fingers are as they leave, 1 = straight, lower = curled (Curl below) - a number for the log, to say
// whether a pose took.
// thumb: how far the thumb follows the fingers (1 = the same; the string hand's hook leaves it loose).
float Curl(const Hero& h, int side, const std::vector<Mat>& w);
void Fist(const Hero& h, int side, float grip, float closed, float open, const std::vector<Mat>& oldW, std::vector<Mat>& newW, float thumb = 1.0f, float* curl = nullptr) {
    const int k = h.hand[side], i = h.ind[side], m = h.mid[side], p = h.pnk[side];
    if (k < 0 || i < 0 || m < 0 || p < 0 || h.fingers[side].empty()) return;
    for (const Hero::Finger& f : h.fingers[side])
        for (const int b : f.bones) {
            const int pa = h.parent[b];
            newW[b] = h.bindOk ? Mul(Mul(h.bind[b], InvAffine(h.bind[pa])), newW[pa]) : Mul(Mul(oldW[b], InvAffine(oldW[pa])), newW[pa]);
        }
    const V3 point = Sub(Pos(newW[m]), Pos(newW[k])), across = Sub(Pos(newW[i]), Pos(newW[p]));
    const V3 palm = Norm(Scale(Cross(point, across), side == 0 ? -1.0f : 1.0f));
    const float amount = open + (closed - open) * std::clamp(grip, 0.0f, 1.0f);   // [fist], per class
    for (const Hero::Finger& f : h.fingers[side]) {
        const int nb = (int)f.bones.size();
        const V3 u = Norm(Sub(Pos(newW[f.bones[f.firstBend + 1]]), Pos(newW[f.bones[f.firstBend]])));
        const V3 axis = Norm(Cross(u, palm));
        const V3 toPalm = Norm(Cross(axis, u));
        const float* deg = f.thumb ? kThumbFistDeg : kFingerFistDeg;
        for (int j = f.firstBend, n = 0; j < nb && n < 3; ++j, ++n) {
            const float t = deg[n] * amount * (f.thumb ? thumb : 1.0f) * 0.0174532925f;
            const Mat R = RotBetween(u, Add(Scale(u, cosf(t)), Scale(toPalm, sinf(t))));
            const V3 P = Pos(newW[f.bones[j]]);
            for (int q = j; q < nb; ++q) {   // this joint and the rest of the finger, about the joint
                Mat& w = newW[f.bones[q]];
                const V3 off = RotOnly(Sub(Pos(w), P), R);
                TurnAboutSelf(w, R);
                w.m[12] = P.x + off.x; w.m[13] = P.y + off.y; w.m[14] = P.z + off.z;
            }
        }
    }
    if (curl) *curl = Curl(h, side, newW);
}

// The share of its length a finger's tip stands from its first bending joint, over the fingers (the thumb left out): 1 straight, less
// the more it curls; -1 when there is nothing to measure.
float Curl(const Hero& h, int side, const std::vector<Mat>& w) {
    float sum = 0.0f;
    int count = 0;
    for (const Hero::Finger& f : h.fingers[side]) {
        if (f.thumb || (int)f.bones.size() < f.firstBend + 2) continue;
        float chain = 0.0f;
        for (int j = f.firstBend; j + 1 < (int)f.bones.size(); ++j) chain += Len(Sub(Pos(w[f.bones[j + 1]]), Pos(w[f.bones[j]])));
        if (!(chain > 1e-5f)) continue;
        sum += std::clamp(Len(Sub(Pos(w[f.bones.back()]), Pos(w[f.bones[f.firstBend]]))) / chain, 0.0f, 1.0f);
        ++count;
    }
    return count ? sum / (float)count : -1.0f;
}

// ---- [weight] grip_fit_fingers (vr/grip_pose.h) ----
// The game's weapon-grip pose of the fingers and the thumb: every finger bone of the hand the game holds the weapon in, as its orientation in THAT HAND'S OWN FRAME (read off the game's
// animation while the hero is calm and the weapon is shown on that wrist) - and put on the hand that TAKES the weapon, in its own frame and with its own bind pose, so a right hand's
// curl becomes the left hand's mirror image whichever way the rig labels its bones' axes.
struct GripPoseKept {
    bool ok = false;
    uintptr_t skel = 0;
    int type = -1, side = 0;                 // the hero's skeleton, the weapon type, the hand it was read off
    std::vector<grippose::M3> coords;        // every finger bone of that hand (h.fingers[side], list order), orientation in the hand's frame
};
SRWLOCK g_gripPoseLock = SRWLOCK_INIT;
GripPoseKept g_gripPose;

grippose::V ToPoseV(const V3& v) { return {v.x, v.y, v.z}; }

// The hand's frame (vr/grip_pose.h HandFrame) from its wrist and first knuckles in the matrices `w`.
bool HandFrameAt(const Hero& h, int side, const std::vector<Mat>& w, grippose::M3* e) {
    const int k = h.hand[side], i = h.ind[side], m = h.mid[side], p = h.pnk[side];
    if (k < 0 || i < 0 || m < 0 || p < 0) return false;
    return grippose::HandFrame(ToPoseV(Pos(w[k])), ToPoseV(Pos(w[m])), ToPoseV(Pos(w[i])), ToPoseV(Pos(w[p])), side, e);
}

// Read the pose off hand `side` in the pose `w` (the game's own matrices).
bool TakeGripPose(const Hero& h, int side, const std::vector<Mat>& w, int type, GripPoseKept* out) {
    if (!h.bindOk || h.fingers[side].empty()) return false;
    grippose::M3 e;
    if (!HandFrameAt(h, side, w, &e)) return false;
    out->coords.clear();
    for (const Hero::Finger& f : h.fingers[side])
        for (const int b : f.bones) {
            grippose::M3 u;
            if (!grippose::Orientation(w[b].m, &u)) return false;
            out->coords.push_back(grippose::Coords(u, e));
        }
    out->ok = true; out->skel = h.skel; out->type = type; out->side = side;
    return true;
}

// The kept pose put on hand `side` (the one that takes the weapon): `out` is a copy of `now` (the pose as it stands, the hand placed) whose finger bones of that hand are set to the
// pose, nothing else touched. Each bone gets the orientation the base hand's counterpart has in the hand's frame (relabelled by the two hands' bind poses), its place from the
// chain (the hand's own bind offsets), the lengths of its axes as they stand. nullptr when it took; else why not (nothing in `out` is then to be used).
const char* GripPoseOnto(const Hero& h, int side, const GripPoseKept& k, const std::vector<Mat>& now, std::vector<Mat>& out) {
    const int base = k.side;
    if (!h.bindOk || base == side || base < 0 || base > 1) return "no bind pose";
    std::vector<grippose::FingerShape> bs, ts;
    size_t total = 0;
    for (const Hero::Finger& f : h.fingers[base]) { bs.push_back({f.kind, f.thumb, (int)f.bones.size()}); total += f.bones.size(); }
    for (const Hero::Finger& f : h.fingers[side]) ts.push_back({f.kind, f.thumb, (int)f.bones.size()});
    std::vector<int> map;
    if (!grippose::MatchFingers(bs, ts, &map)) return "the two hands' fingers do not answer each other";
    if (k.coords.size() != total) return "the kept pose is of another hand";
    grippose::M3 eBase, eBind, eNow;
    if (!HandFrameAt(h, base, h.bind, &eBase) || !HandFrameAt(h, side, h.bind, &eBind) || !HandFrameAt(h, side, now, &eNow)) return "no hand frame";
    size_t at = 0;
    for (size_t i = 0; i < h.fingers[base].size(); ++i) {
        const Hero::Finger& fb = h.fingers[base][i];
        const Hero::Finger& ft = h.fingers[side][(size_t)map[i]];
        for (size_t q = 0; q < fb.bones.size(); ++q, ++at) {
            const int bb = fb.bones[q], bt = ft.bones[q], pa = h.parent[bt];
            grippose::M3 ub, ut;
            if (pa < 0 || !grippose::Orientation(h.bind[bb].m, &ub) || !grippose::Orientation(h.bind[bt].m, &ut)) return "a finger bone has no axes";
            const grippose::M3 p = grippose::Relabel(grippose::Coords(ub, eBase), grippose::Coords(ut, eBind));
            const grippose::M3 u = grippose::TakerOrientation(k.coords[at], p, eNow);
            if (!grippose::Finite(u)) return "a bone came out not a number";
            // its place from the chain (the bind pose's own offset in the parent's frame, the parent as just set), its axes the pose's, as long as they stand now
            Mat nw = Mul(Mul(h.bind[bt], InvAffine(h.bind[pa])), out[pa]);
            for (int r = 0; r < 3; ++r) {
                const float len = (float)boneguard::RowLen(now[bt].m, r);
                for (int c = 0; c < 3; ++c) nw.m[r * 4 + c] = len * u.m[r * 3 + c];
                nw.m[r * 4 + 3] = 0.0f;
            }
            nw.m[15] = 1.0f;
            // a bone keeps the handedness it has (a left hand's are mirrors): the maths gives the taker bone's own, or something is wrong
            if ((boneguard::Det(nw.m) < 0.0) != (boneguard::Det(now[bt].m) < 0.0)) return "a bone came out of the other handedness";
            for (float v : nw.m)
                if (!std::isfinite(v)) return "a bone came out not a number";
            out[bt] = nw;
        }
    }
    return nullptr;
}

// bowhook::BlendBone for a bone that may be a mirror (a left hand's: determinant < 0): the first axis is turned over for the blend and back after, so the rotation is blended as a proper one.
void BlendBoneHanded(const Mat& a, const Mat& b, float t, Mat* out) {
    Mat x = a, y = b, r{};
    const bool mirror = boneguard::Det(a.m) < 0.0;
    if (mirror) for (int c = 0; c < 3; ++c) { x.m[c] = -x.m[c]; y.m[c] = -y.m[c]; }
    bowhook::BlendBone(x.m, y.m, t, r.m);
    if (mirror) for (int c = 0; c < 3; ++c) r.m[c] = -r.m[c];
    *out = r;
}

// ---- [weight] grip_model 1: the grip library's hand (vr/grip_rig.h; the C interface is shared/d2r_vr_grip.h) ----
// The library decides where a hand sits on the wood and how its fingers close; the hook gives it the hand's rig (the finger bones in the hand's own proper frame, open and closed), the
// hand as the weight model has it and the shaft as drawn, and lays the hand and the fingers it answers with on the bones. The pose of the second eye of a pair gets the first's answer:
// one library step a sample.
struct LibGripHand {
    uint64_t key = 0;
    bool rigOk = false;
    const char* why = nullptr;
    D2rgRig rig{};
    std::vector<int> mirror;
    D2rgHandState st{};
    D2rgHandOut out{};
    uint32_t sample = 0;
    bool have = false;
    LARGE_INTEGER at{};
    bool haveAt = false;
};
SRWLOCK g_libGripLock = SRWLOCK_INIT;
LibGripHand g_libGrip[2];

// What the hook keeps of the library's answer for a hand in this pose: the hand to lay, and the fingers for the stage after the hand is laid.
struct LibGripResult {
    bool ok = false;
    D2rgHandOut out{};
    D2rgRig rig{};
    std::vector<int> mirror;
};

// A hand's rig for the library: its finger bones in the hand's proper frame, open (the bind pose) and closed (the game's grip pose where it was read - for the hand it was read off as it
// is, for the other by the relabelling - else the fist table's); the bones before a finger's first bending one (a metacarpal) are not bent. nullptr when it is built; else why not.
const char* LibBuildRig(const Hero& h, int side, const GripPoseKept& kept, const Input& in, const std::vector<Mat>& now, float upm, LibGripHand* L) {
    if (!h.bindOk || h.hand[side] < 0 || h.mid[side] < 0 || h.fingers[side].empty()) return "no bind pose, or the hand has no fingers";
    if (h.fingers[side].size() > (size_t)D2RG_MAX_FINGERS) return "the hand has more fingers than the library takes";
    size_t total = 0;
    for (const Hero::Finger& f : h.fingers[side]) total += f.bones.size();
    if (total < 1 || total > (size_t)D2RG_MAX_BONES) return "the hand has more finger bones than the library takes";
    grippose::M3 eBind, eNow;
    if (!HandFrameAt(h, side, h.bind, &eBind) || !HandFrameAt(h, side, now, &eNow)) return "no hand frame";
    const grippose::M3 epBind = griprig::ProperFrame(eBind, side);
    const V3 palmBind = Scale(Add(Pos(h.bind[h.hand[side]]), Pos(h.bind[h.mid[side]])), 0.5f);
    // the closed pose: every finger bone's orientation as coordinates in the hook's hand frame, in list order
    std::vector<grippose::M3> closed;
    const auto coordsOf = [&](const std::vector<Mat>& w) {
        closed.clear();
        for (const Hero::Finger& f : h.fingers[side])
            for (const int b : f.bones) {
                grippose::M3 u;
                if (!grippose::Orientation(w[b].m, &u)) return false;
                closed.push_back(grippose::Coords(u, eNow));
            }
        return true;
    };
    bool have = false;
    if (kept.ok && kept.skel == h.skel && kept.type == in.weaponType) {
        if (kept.side == side && kept.coords.size() == total) {
            closed = kept.coords;
            have = true;
        } else if (kept.side != side) {
            std::vector<Mat> tmp = now;
            if (GripPoseOnto(h, side, kept, now, tmp) == nullptr) have = coordsOf(tmp);
        }
    }
    if (!have) {   // no game pose: the fist table's
        std::vector<Mat> tmp = now;
        const int cls = h.cls >= 0 ? h.cls : 3;
        Fist(h, side, 1.0f, in.fistClose[cls], in.fistOpen[cls], now, tmp);
        if (!coordsOf(tmp)) return "a finger bone has no axes";
    }
    std::vector<griprig::Bone> bones;
    size_t idx = 0;
    for (size_t fi = 0; fi < h.fingers[side].size(); ++fi) {
        const Hero::Finger& f = h.fingers[side][fi];
        for (size_t j = 0; j < f.bones.size(); ++j, ++idx) {
            const int b = f.bones[j];
            griprig::Bone bo;
            bo.finger = (int)fi;
            bo.parent = j == 0 ? -1 : (int)bones.size() - 1;
            grippose::M3 u;
            if (!grippose::Orientation(h.bind[b].m, &u)) return "a finger bone has no axes";
            bo.open = griprig::CoordsInProper(grippose::Coords(u, eBind), side);
            bo.closed = (int)j < f.firstBend ? bo.open : griprig::CoordsInProper(closed[idx], side);
            const V3 d = Sub(Pos(h.bind[b]), palmBind);
            const grippose::V dv{d.x, d.y, d.z};
            bo.pos = {grippose::Dot(dv, epBind.Row(0)), grippose::Dot(dv, epBind.Row(1)), grippose::Dot(dv, epBind.Row(2))};
            bones.push_back(bo);
        }
    }
    if (!griprig::BuildRig(bones, (int)h.fingers[side].size(), (double)upm, &L->rig, &L->mirror)) return "the hand's bones do not make a rig (a bone a mirror in one pose and not in the other, or a bad chain)";
    return nullptr;
}

// One step of hand `side` for this sample (the second eye of a pair gets the first's answer). hin: everything but dt (set here). nullptr when `res` is the library's answer; else why not.
const char* LibGripRun(const Hero& h, int side, const Input& in, const GripPoseKept& kept, const std::vector<Mat>& now, float upm, D2rgHandIn hin, LibGripResult* res) {
    const GripHandStepFn step = g_gripHandStep.load();
    if (!step) return "no hand step registered";
    AcquireSRWLockExclusive(&g_libGripLock);
    LibGripHand& L = g_libGrip[side];
    const uint64_t key = ((uint64_t)h.skel * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)(uint32_t)(in.weaponType + 7) << 40) ^ ((uint64_t)(side + 1) << 56) ^
                         (kept.ok ? (((uint64_t)(kept.side + 3) << 16) ^ ((uint64_t)(uint32_t)kept.type << 24) ^ 0x1) : 0x55);
    if (L.key != key) {   // another hero, hand, weapon, or the game's pose has come: the rig again, the grab afresh
        L.key = key;
        L.why = LibBuildRig(h, side, kept, in, now, upm, &L);
        L.rigOk = L.why == nullptr;
        L.st = D2rgHandState{};
        L.have = false;
        L.haveAt = false;
    }
    if (!L.rigOk) {
        const char* why = L.why;
        ReleaseSRWLockExclusive(&g_libGripLock);
        return why;
    }
    if (!(L.have && L.sample == in.sample)) {
        LARGE_INTEGER t, f;
        QueryPerformanceCounter(&t);
        QueryPerformanceFrequency(&f);
        const double dt = L.haveAt ? std::clamp((double)(t.QuadPart - L.at.QuadPart) / (double)f.QuadPart, 0.0, 0.05) : 0.0;
        L.at = t;
        L.haveAt = true;
        hin.size = sizeof hin;
        hin.dt = dt;
        {   // the fingers as the hand has them now: the grab closes them from there
            grippose::M3 eNow;
            std::vector<grippose::M3> coords;
            bool ok = HandFrameAt(h, side, now, &eNow);
            for (const Hero::Finger& f : h.fingers[side])
                for (const int b : f.bones) {
                    grippose::M3 u;
                    if (!ok || !grippose::Orientation(now[b].m, &u)) { ok = false; continue; }
                    coords.push_back(griprig::CoordsInProper(grippose::Coords(u, eNow), side));
                }
            D2rgQ q[D2RG_MAX_BONES];
            if (ok && griprig::LocalsFromCoords(L.rig, L.mirror, coords, q)) {
                hin.hasFingersFrom = 1;
                for (int i = 0; i < L.rig.nBones; ++i) hin.fingersFrom[i] = q[i];
            }
        }
        if (step(hin, L.rig, &L.st, &L.out) < 0) {
            ReleaseSRWLockExclusive(&g_libGripLock);
            return "the library refused the step";
        }
        L.sample = in.sample;
        L.have = true;
    }
    res->ok = true;
    res->out = L.out;
    res->rig = L.rig;
    res->mirror = L.mirror;
    ReleaseSRWLockExclusive(&g_libGripLock);
    return nullptr;
}

// A hand that holds nothing: its library state is forgotten, the next grab starts afresh.
void LibGripForget(int side) {
    AcquireSRWLockExclusive(&g_libGripLock);
    g_libGrip[side].st = D2rgHandState{};
    g_libGrip[side].have = false;
    g_libGrip[side].haveAt = false;
    ReleaseSRWLockExclusive(&g_libGripLock);
}

// [weight] grab_wrap: the same hand step for a hand that holds a body ([weight] grab_bodies) - its own state (a grab is not a weapon grip), the rig closing to the fist
// table's pose (no game's weapon-grip pose for a body), the held limb's capsule as the "shaft". nullptr when `res` is the library's answer; else why not.
LibGripHand g_libGrab[2];
const char* LibGrabRun(const Hero& h, int side, const Input& in, const std::vector<Mat>& now, float upm, D2rgHandIn hin, LibGripResult* res) {
    const GripHandStepFn step = g_gripHandStep.load();
    if (!step) return "no grip library (d2r_vr_grip.dll) - the palm is laid on, the fist closes";
    AcquireSRWLockExclusive(&g_libGripLock);
    LibGripHand& L = g_libGrab[side];
    const uint64_t key = ((uint64_t)h.skel * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)(side + 1) << 56) ^ 0x6772616275ull;
    if (L.key != key) {
        L.key = key;
        L.why = LibBuildRig(h, side, GripPoseKept{}, in, now, upm, &L);
        L.rigOk = L.why == nullptr;
        L.st = D2rgHandState{};
        L.have = false;
        L.haveAt = false;
    }
    if (!L.rigOk) {
        const char* why = L.why;
        ReleaseSRWLockExclusive(&g_libGripLock);
        return why;
    }
    if (!(L.have && L.sample == in.sample)) {
        LARGE_INTEGER t, f;
        QueryPerformanceCounter(&t);
        QueryPerformanceFrequency(&f);
        hin.size = sizeof hin;
        hin.dt = L.haveAt ? std::clamp((double)(t.QuadPart - L.at.QuadPart) / (double)f.QuadPart, 0.0, 0.05) : 0.0;
        L.at = t;
        L.haveAt = true;
        {   // the fingers as the hand has them now: the grab closes them from there (smoothly), not from the open pose
            grippose::M3 eNow;
            std::vector<grippose::M3> coords;
            bool ok = HandFrameAt(h, side, now, &eNow);
            for (const Hero::Finger& fi : h.fingers[side])
                for (const int b : fi.bones) {
                    grippose::M3 u;
                    if (!ok || !grippose::Orientation(now[b].m, &u)) { ok = false; continue; }
                    coords.push_back(griprig::CoordsInProper(grippose::Coords(u, eNow), side));
                }
            D2rgQ q[D2RG_MAX_BONES];
            if (ok && griprig::LocalsFromCoords(L.rig, L.mirror, coords, q)) {
                hin.hasFingersFrom = 1;
                for (int i = 0; i < L.rig.nBones; ++i) hin.fingersFrom[i] = q[i];
            }
        }
        if (step(hin, L.rig, &L.st, &L.out) < 0) {
            ReleaseSRWLockExclusive(&g_libGripLock);
            return "the grip library (d2r_vr_grip.dll) is not there or refused the step";
        }
        L.sample = in.sample;
        L.have = true;
    }
    res->ok = true;
    res->out = L.out;
    res->rig = L.rig;
    res->mirror = L.mirror;
    ReleaseSRWLockExclusive(&g_libGripLock);
    return nullptr;
}
void LibGrabForget(int side) {
    AcquireSRWLockExclusive(&g_libGripLock);
    g_libGrab[side].st = D2rgHandState{};
    g_libGrab[side].have = false;
    g_libGrab[side].haveAt = false;
    ReleaseSRWLockExclusive(&g_libGripLock);
}

// [bow] hook_game_pose: the string hand's fingers as the game's own bow-draw animation holds them (vr/bow_hook.h: the measured rotation of
// every finger bone, bind pose -> the hold of the draw, in the bone's own frame). Laid on the chains below the wrist bone `newW[hand]` as it
// stands now, into `hook` (a copy of the finger bones' matrices, nothing else touched): the knuckles open, the last two joints of each finger
// curled, the thumb turned in. The Amazon's right hand only - the rig whose bones the measurement is of, and the hand she draws with; false
// for any other, and for a hand whose chains it cannot name (then the fist-table hook stays).
bool HookPose(const Hero& h, int side, const std::vector<Mat>& newW, std::vector<Mat>& hook, const bowhook::Tune& tune = {}) {
    if (side != 0 || h.cls != 0 || !h.bindOk || h.hand[0] < 0) return false;
    int named = 0, fingers = 0;
    for (const Hero::Finger& f : h.fingers[side]) {
        if (f.kind < 0) continue;
        ++named;
        if (f.kind != bowhook::kThumb) ++fingers;
    }
    if (fingers < 4) return false;
    for (const Hero::Finger& f : h.fingers[side]) {
        if (f.kind < 0) continue;
        for (int j = 0; j < (int)f.bones.size(); ++j) {
            const int b = f.bones[j], pa = h.parent[b];
            const Mat parent = j == 0 ? newW[h.hand[side]] : hook[pa];
            const Mat local = Mul(h.bind[b], InvAffine(h.bind[pa]));
            Mat rel{};
            bowhook::RowMatrix(bowhook::Tuned(f.kind, j, tune), rel.m);
            hook[b] = Mul(Mul(rel, local), parent);
        }
    }
    (void)named;
    return true;
}

#endif
#if D2RVR_FIRST_PERSON
struct StringAnchor;
void NoteBowHands(uintptr_t self, const Hero& h, const Input& in, const std::vector<Mat>& oldW, const std::vector<Mat>& newW, float c, float s, const StringAnchor& anchor);   // [bow] rig_pose, below
// [bow] hand_pose: the draw detector's feed, if it is under 150 ms old: the hand that takes the string (0 right, 1 left; -1 not told)
// and whether the string is taken. Below.
int BowFeedSide(bool* drawing);
// ... and the string hand's own trigger and squeeze (SetBowHandControls), if they are under 150 ms old and for this very hand.
bool BowHandRaw(int side, float* trigger, float* squeeze);
std::atomic<const char*> g_handPoseWhy{"not asked yet"};   // what the hand pose did last, for vrcam's log
std::atomic<int> g_handPoseSide{-1};
std::atomic<const char*> g_handPoseHow{""};
// ... and with what: the squeeze asked for (0..1) and where it came from (bowhand::Source), the eased one used, the fist's bend it made, how
// curled the fingers were when Fist was done with them and as they were written (Curl: 1 straight, -1 unknown), and the hook asked for.
std::atomic<float> g_handPoseTarget{0.0f}, g_handPoseUsed{0.0f}, g_handPoseAmount{0.0f}, g_handPoseHook{0.0f};
std::atomic<float> g_handPoseCurl{-1.0f}, g_handPoseCurlOut{-1.0f};
std::atomic<float> g_handPoseGame{-1.0f};   // [bow] hook_game_pose: how far the game's own draw pose is laid on the hand (0..1; -1 = not used: the fist table's hook)
std::atomic<int> g_handPoseSource{0};
std::atomic<float> g_handPoseAttach[2] = {-1.0f, -1.0f};   // the weapon attach bones' sizes at the wrists (R, L): 1.00/1.00 = both shown, no telling which holds the bow
// The wrist the game hung the bow on is the one whose attach bone it shows (the other it shrinks to nothing): 0 right, 1 left,
// -1 when it cannot be told. The other hand is the one that takes the string.
int BowSideOf(const Hero& h, const std::vector<Mat>& oldW, float* sizes = nullptr) {
    if (h.weapon[0] < 0 || h.weapon[1] < 0 || h.hand[0] < 0 || h.hand[1] < 0) return -1;
    float size[2];
    for (int side = 0; side < 2; ++side) {
        const Mat a = Mul(oldW[h.weapon[side]], InvAffine(oldW[h.hand[side]]));
        size[side] = Len({a.m[0], a.m[1], a.m[2]});
    }
    if (sizes) { sizes[0] = size[0]; sizes[1] = size[1]; }
    return size[1] > 0.7f && size[0] < 0.3f ? 1 : size[0] > 0.7f && size[1] < 0.3f ? 0 : -1;
}
// [hands] shoulder_draw_ranged: a bow or crossbow that is put away is not drawn. Its attach bone is the one the game shows (the other is shrunk); with both shown (an Amazon's quiver in the
// other slot) the slot's (Input::rangedBone). Shrunk to a point and what hangs on it follows (Propagate); a hero without that bone draws it as the game does.
void HideRanged(const Hero& h, const Input& in, const std::vector<Mat>& oldW, std::vector<Mat>& newW) {
    int side = BowSideOf(h, oldW);
    if (side < 0) side = in.rangedBone == 0 ? 0 : 1;
    const int wb = h.weapon[side];
    if (wb < 0) return;
    const std::vector<Mat> before = newW;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) newW[wb].m[r * 4 + c] *= 0.001f;
    std::vector<char> fromS(h.count, 0);
    fromS[wb] = 1;
    Propagate(h, before, newW, fromS, wb);
}
// The wrist the bow hangs on for the bow's rig and the aim: BowSideOf's when the attach bones tell, else the hand opposite the string
// hand the draw detector names, else the one opposite vrcam's string side (Input::bowStringSide - the bow is in the aim hand).
// An Amazon's quiver sits in the other slot and both attach bones are shown, so the bones alone never told hers. `sizes` get the bones'.
bowaim::BowWrist BowWristNow(const Hero& h, const Input& in, const std::vector<Mat>& oldW, float sizes[2]) {
    sizes[0] = sizes[1] = -1.0f;
    BowSideOf(h, oldW, sizes);
    // [hands] shoulder_draw_ranged_swap: a bow drawn into the other hand is held in THAT hand, whatever attach bone the game shows (BowCarry moves it there)
    if (in.rangedHolster && in.rangedBow && in.rangedSwap && (in.rangedSide == 0 || in.rangedSide == 1)) return {in.rangedSide, bowaim::SideBy::AimHand};
    bool drawing = false;
    const int fed = BowFeedSide(&drawing);
    return bowaim::ChooseBowWrist(sizes[0], sizes[1], fed, in.bowStringSide == 0 || in.bowStringSide == 1 ? in.bowStringSide ^ 1 : -1);
}
// The side of the attach bone the bow's MESH hangs on: the game's own - the bone it shows, else (both shown: an Amazon's quiver in the other slot) the slot's (Input::rangedBone). With the
// swap the bow is held in the other wrist but the mesh stays on this bone (BowCarry moves the bone there); without it, the wrist the bow is in.
int BowBoneSide(const Hero& h, const Input& in, const std::vector<Mat>& oldW, int wristSide) {
    if (!(in.rangedHolster && in.rangedBow && in.rangedSwap)) return wristSide;
    const int shown = BowSideOf(h, oldW);
    return shown >= 0 ? shown : (in.rangedBone & 1);
}
// [hands] shoulder_draw_ranged_swap: the bow's attach bone (the game hangs the item on `boneSide`'s, in `boneSide`'s hand) carried to the OTHER hand, mirrored: the bone keeps its relation to
// the hand it has been put in (the pose the game and the arms gave it), seen from the other hand - the two hands are each other's mirror image in the bind pose, so a point of the one
// hand's frame goes to the same point of the other's through  bind[from] * Reflect * inv(bind[to])  (the plane between the hands). That is the bow reflected in the body's plane; the left
// attach bone is a mirror (negative determinant) and the item on it is drawn from a matrix of that handedness only (inside out otherwise), so it is reflected once more through the bow's own
// plane of symmetry (its side axis, local) - which leaves the bow where it is and gives the matrix its handedness back. Children of the bone follow (Propagate). False: nothing done.
bool BowCarry(const Hero& h, const std::vector<Mat>& oldW, std::vector<Mat>& newW, int boneSide, int toWrist, const float sideAxis[3]) {
    if (boneSide < 0 || toWrist < 0 || boneSide == toWrist || !h.bindOk) return false;
    const int mb = h.weapon[boneSide], from = h.hand[boneSide], to = h.hand[toWrist];
    if (mb < 0 || from < 0 || to < 0 || h.hand[0] < 0 || h.hand[1] < 0) return false;
    const float x0 = 0.5f * (Pos(h.bind[h.hand[0]]).x + Pos(h.bind[h.hand[1]]).x);
    Mat reflect{};
    reflect.m[0] = -1.0f; reflect.m[5] = 1.0f; reflect.m[10] = 1.0f; reflect.m[15] = 1.0f; reflect.m[12] = 2.0f * x0;
    const Mat rel = Mul(newW[mb], InvAffine(newW[from]));
    Mat m = Mul(Mul(Mul(rel, h.bind[from]), Mul(reflect, InvAffine(h.bind[to]))), newW[to]);
    // the handedness the bone had (a mirror on the left, a proper one on the right); a bone shrunk to a point keeps its sign
    const double was = boneguard::Det(oldW[mb].m);
    const int want = was < -1e-12 ? -1 : was > 1e-12 ? 1 : (boneSide == 1 ? -1 : 1);
    const double now = boneguard::Det(m.m);
    if ((now < 0.0 ? -1 : 1) != want) {
        V3 sd = sideAxis ? Norm(V3{sideAxis[0], sideAxis[1], sideAxis[2]}) : V3{1.0f, 0.0f, 0.0f};   // (no bow rig read yet: its first local axis)
        Mat refl{};
        const float a[3] = {sd.x, sd.y, sd.z};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) refl.m[i * 4 + j] = (i == j ? 1.0f : 0.0f) - 2.0f * a[i] * a[j];
        refl.m[15] = 1.0f;
        m = Mul(refl, m);
    }
    for (float v : m.m)
        if (!std::isfinite(v)) return false;
    const std::vector<Mat> before = newW;
    newW[mb] = m;
    std::vector<char> fromS(h.count, 0);
    fromS[mb] = 1;
    Propagate(h, before, newW, fromS, mb);
    return true;
}
// ... and the fingers go with it: each hand's finger pose as the game animated it (the bow hand's grip on the bow, the string hand's) is laid on the OTHER hand, mirrored (GripPoseOnto, the
// weight model's way of passing a weapon's grip to the hand that takes it) - the hand the bow was drawn into closes on it, the other has the game's string hand. A hand whose pose cannot be
// taken or laid (no fingers found, the two hands do not answer each other) keeps what it has.
thread_local bool g_fingersCarried = false;   // this pose: CarryBowFingers has laid the hands' finger poses on the other hands - the string anchor reads them from newW, not from the game's own (the other hand's)
bool CarryBowFingers(const Hero& h, const std::vector<Mat>& oldW, std::vector<Mat>& newW, int a, int b) {
    if (a < 0 || b < 0 || a == b || !h.bindOk) return false;
    bool any = false;
    std::vector<Mat> laid = newW;
    for (int pass = 0; pass < 2; ++pass) {
        const int from = pass ? b : a, to = pass ? a : b;
        GripPoseKept kept;
        if (!TakeGripPose(h, from, oldW, 0, &kept)) continue;
        std::vector<Mat> one = newW;   // (read off the game's own hands, put on the other as it stands now)
        if (GripPoseOnto(h, to, kept, newW, one)) continue;
        for (const Hero::Finger& f : h.fingers[to])
            for (const int bone : f.bones) laid[bone] = one[bone];
        any = true;
    }
    newW = laid;   // (only finger bones differ)
    return any;
}
// The final attach bone `w` (model space, as ours left it) into the game's world: the hero's own model-to-world
// (his TransformComponent's local matrix, set before this very pose - a later copy displaced fast swings) times the
// bone. Kept with the stamp of the hand sample this pose is made from, so a swing is matched to the hand that posed
// it. Idea and shape after bearstonem/d2r-pcvr (MIT) skeletons.cpp MeleeWeapon. Read-only on the game.
// [body] under_camera (vr/under_camera.h): the up part of the body's move under the camera (model units; a Leap's arc pulled back down to
// the camera's ground, or followed by it). The pose is worked out with the body's feet at the model's own height - the eye's height in
// model space is read as its height over the feet everywhere (the hands' scale, the arm span, the reach) - and the finished bones are
// raised by this just before they are written (PoseBody's end). A read of the hero's model-to-world within the pose goes through
// PoseModel, which carries the same move: what is taken to the world from a bone of the pose is where it will be drawn. 0 outside a pose.
thread_local float t_poseUp = 0.0f;
void PoseModel(Mat& model) {
    if (t_poseUp == 0.0f) return;
    for (int k = 0; k < 3; ++k) model.m[12 + k] += t_poseUp * model.m[4 + k];   // (0, up, 0) through the model's own rows
}

void NotePosedWeapon(uintptr_t self, const Mat& w, int side, const Input& in, const PosedBlade (&blades)[2]) {
    PosedWeaponPose out;
    out.blade[0] = blades[0]; out.blade[1] = blades[1];
    bool ok = false;
    HeroXform hx; Mat model;
    if (in.havePoseStamp && HeroTransform(&hx) && hx.self == self && (hx.parent & 0xFFFFFu) == 0xFFFFFu &&
        SafeCopy(model.m, (const void*)hx.comp, sizeof model)) {
        PoseModel(model);
        const Mat world = Mul(w, model);
        const V3 o = Pos(world), a = Norm({world.m[4], world.m[5], world.m[6]}), p = Pos(model);
        if (Len({world.m[4], world.m[5], world.m[6]}) > 0.001f && std::isfinite(o.x + o.y + o.z + a.x + a.y + a.z + p.x + p.y + p.z)) {
            out.origin[0] = o.x; out.origin[1] = o.y; out.origin[2] = o.z;
            out.axis[0] = a.x; out.axis[1] = a.y; out.axis[2] = a.z;
            out.hero[0] = p.x; out.hero[1] = p.y; out.hero[2] = p.z;
            out.poseStamp = in.poseStamp; out.worldEpoch = in.worldEpoch; out.heroGeneration = HeroGen();
            out.weaponType = (uint32_t)in.weaponType; out.side = side;
            ok = true;
        }
    }
    AcquireSRWLockExclusive(&g_posedLock);
    g_posed = out;
    g_posedAt = ok ? GetTickCount64() : 0;
    ReleaseSRWLockExclusive(&g_posedLock);
}

// ---- [bow] aim_along_hands: the bow lies along the line from the string hand to the bow hand ----
// The bow is held as the game's animation holds it: rigid in the left hand, turned the way the controller is. While the string is taken
// (the draw feed says so) the bow's own forward axis - the one the string is drawn back along, from the grip toward the archer's
// opposite - is turned onto the line from the string hand to the bow hand, the line the shot goes along; what the player sees and
// what the game aims then agree. The turn is made on the bow hand's plan - the two directions its wrist is turned by - so the hand,
// the forearm that reaches it, the attach bone, the bow and the arrow on its string (vr/bow_rig.h draws it along that line) all
// follow: the arm is solved again for the turned hand, with the palm still at the controller. vr/bow_aim.h has the maths; it comes in
// and goes out over [bow] aim_blend_ms at the take, the release and a cancel. Only this pose's bone matrices of the hero; no game state.
std::atomic<bool> g_bowAimOn{false};                 // Set(): [bow] aim_along_hands, or the debug wanting the bow's axis, with the arms on the controllers
SRWLOCK g_bowAimLock = SRWLOCK_INIT;                 // what follows
bowaim::Blend g_aimBlend;
struct BowAimStat { bool on = false; float blend = 0.0f, angle = 0.0f, raw = 0.0f; const char* why = "not asked yet"; ULONGLONG at = 0; std::string who; };
BowAimStat g_aimStat;
float g_aimAxis[3] = {0.0f, 0.0f, -1.0f};            // the bow's forward axis as posed, in the hands' frame
ULONGLONG g_aimAxisAt = 0;
float g_bowAxF[3] = {0.0f, 0.0f, 1.0f}, g_bowAxU[3] = {0.0f, 1.0f, 0.0f}, g_bowAxS[3] = {1.0f, 0.0f, 0.0f};   // the bow's forward, limb and side (up x forward) axes in its attach bone's frame, read by its own hook
ULONGLONG g_bowAxesAt = 0;
// [bow] arrow_rest_*: the arrow's rest point (vr/bow_aim.h SolveRest) as the last hero pose made it - in the hero's model space for the bow's
// own hook (the arrow's line is from the string hand through it), in the frame of the hands for the draw's aim and the debug dot.
bowaim::Blend g_restBlend;                           // how far the offset is in (the draw's take and release: it comes in and goes out, nothing jumps)
float g_restModel[3] = {}, g_restHands[3] = {};
ULONGLONG g_restAt = 0;
bowaim::Smooth g_restLiftSmooth;                     // [bow] arrow_rest_clear: how far the arrow and the nock are lifted off the arm, smoothed
float g_restLift[3] = {};                            // ... as a vector in the hero's model space (along the bow's displayed limb axis)
// [bow] arrow_rest_model: the bow model's own arrow rest (bowrig::ArrowRestPoint) in its attach bone's frame, as the bow's own hook last read it (the hero's
// pose carries it by the attach bone to where the bow hangs now)
float g_restAttach[3] = {};
ULONGLONG g_restAttachAt = 0;
float g_restResidual = 0.0f;
int g_restSteps = 0;
bool g_restSettled = true;

bool BowAxesNow(float f[3], float u[3], float s[3]) {
    AcquireSRWLockShared(&g_bowAimLock);
    const bool fresh = g_bowAxesAt && GetTickCount64() - g_bowAxesAt < 300;
    if (fresh) { memcpy(f, g_bowAxF, sizeof g_bowAxF); memcpy(u, g_bowAxU, sizeof g_bowAxU); memcpy(s, g_bowAxS, sizeof g_bowAxS); }
    ReleaseSRWLockShared(&g_bowAimLock);
    return fresh;
}

// [bow] string_anchor: the bow's string is held at the first joints of the fingers (index, middle, ring: vr/bow_hook.h kAnchorJoint), not at the controller's grip point
// (the middle of the palm, about a hand's length behind them - the string then met the base of the hand and hooked nothing). With hand_pose + hook_game_pose the joints are
// those of the hooked pose HookPose lays on the string hand's wrist as it stands now (the fingers sit on the wrist rigidly, so this does not wait for them to be posed,
// which comes later in the pose) - the Amazon's right hand; that path is exactly as it was. With hand_pose OFF the fingers are the game's own animation of them (which,
// for a bow in hand, is the hook already), put on the wrist as it stands now. Either way the sliders move the point along the hand's own axes.
bowhook::Tune HookTune(const Input& in) {
    bowhook::Tune t;
    t.curl = in.bowHookCurl;
    t.thumbDeg = in.bowHookThumbDeg;
    return t;
}

bool FingerAnchor(const Hero& h, int side, const Input& in, const std::vector<Mat>& oldW, const std::vector<Mat>& newW, float upm, V3* out, std::string* source, const char** why) {
    static thread_local std::vector<Mat> laid;   // the finger bones' world matrices as they will stand on the wrist as it stands now (nothing else of it is read)
    if (laid.size() != newW.size()) laid.assign(newW.size(), Mat{});
    if (in.bowHandPose) {   // the hooked pose of hand_pose + hook_game_pose (StringAnchorNow has seen to the second)
        if (!HookPose(h, side, newW, laid, HookTune(in))) { *why = "the hooked pose is not known for this hand (the Amazon's right hand only)"; return false; }
    } else {                // hand_pose off: the game's own animation of the fingers, on the wrist as it stands now (each bone's animated local on its parent)
        // (a bow drawn into the other hand, shoulder_draw_ranged_swap: the fingers on this wrist are the other hand's pose laid on it - newW has them)
        const std::vector<Mat>& anim = g_fingersCarried ? newW : oldW;
        for (const Hero::Finger& f : h.fingers[side])
            for (size_t j = 0; j < f.bones.size(); ++j) {
                const int b = f.bones[j], pa = h.parent[b];
                const Mat parent = j == 0 ? newW[(size_t)h.hand[side]] : laid[(size_t)pa];
                laid[(size_t)b] = Mul(Mul(anim[(size_t)b], InvAffine(anim[(size_t)pa])), parent);
            }
    }
    bowhook::Vec joints[3];
    int bones[3] = {-1, -1, -1};
    int count = 0;
    for (const Hero::Finger& f : h.fingers[side])
        if (bowhook::InAnchor(f.kind) && (int)f.bones.size() > bowhook::kAnchorJoint && count < 3) {
            const V3 q = Pos(laid[f.bones[bowhook::kAnchorJoint]]);
            bones[count] = f.bones[bowhook::kAnchorJoint];
            joints[count++] = {q.x, q.y, q.z};
        }
    const int k = h.hand[side], m = h.mid[side], i = h.ind[side], p = h.pnk[side];
    if (count < 1 || k < 0 || m < 0 || i < 0 || p < 0) { *why = "the fingers' joints were not found"; return false; }
    const auto v = [&](int bone) { const V3 q = Pos(newW[bone]); return bowhook::Vec{q.x, q.y, q.z}; };
    const bowhook::Frame frame = bowhook::HandFrame(v(k), v(m), v(i), v(p), side);
    bowhook::Vec point;
    if (!bowhook::AnchorPoint(joints, count, frame, in.bowAnchorCm[0], in.bowAnchorCm[1], in.bowAnchorCm[2], upm, &point)) { *why = "the hand has no frame"; return false; }
    *out = {point.x, point.y, point.z};
    if (source) {
        const auto name = [&](int b) { return b >= 0 && b < (int)h.boneNames.size() ? h.boneNames[(size_t)b].c_str() : "?"; };
        char text[300];
        snprintf(text, sizeof text, "the first joints of the index, middle and ring fingers (bones %s, %s, %s), %s",
                 name(bones[0]), count > 1 ? name(bones[1]) : "-", count > 2 ? name(bones[2]) : "-",
                 in.bowHandPose ? "as the hooked pose lays them (hand_pose on)" : "as the game animates them (hand_pose off)");
        *source = text;
    }
    return true;
}

struct StringAnchor {
    bool ok = false;           // `point` is where the string is held (else the grip point is)
    int side = -1;             // the string hand
    V3 point{};                // model space: the grip point drawn toward the fingers' first joints by `blend`
    float blend = 0.0f;        // 0..1, eased with the draw
    float cm = 0.0f;           // how far from the grip point, cm
    const char* why = "off";
};
bowaim::Blend g_anchorBlend;   // (under g_bowAimLock, as is what follows)
float g_anchorHands[3] = {};   // the string's place in the hands' frame, for the draw's aim line (StringAnchorHands)
ULONGLONG g_anchorAt = 0;
std::string g_anchorWho;       // what the anchor is made of, for the log (StringAnchorWho)

StringAnchor StringAnchorNow(const Hero& h, const Input& in, const std::vector<Mat>& oldW, const std::vector<Mat>& newW, float c, float s) {
    StringAnchor a;
    if (!in.bowStringAnchor) return a;
    if (in.bowHandPose && !in.bowHookGame) { a.why = "with the string hand's pose on it needs the game's draw pose (hook_game_pose)"; return a; }
    if (h.head < 0) { a.why = "no head bone"; return a; }
    float sizes[2];
    const int bowSide = BowWristNow(h, in, oldW, sizes).side;
    if (bowSide < 0) { a.why = "the bow's hand is not known"; return a; }
    const int side = bowSide ^ 1;
    if (!(in.handsValid & (1u << side))) { a.why = "the string hand's controller is not tracked"; return a; }
    bool drawing = false;
    BowFeedSide(&drawing);
    drawing = drawing || (in.bowRigGrip && in.grip[side] > 0.6f);
    const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
    const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
    V3 joint{};
    const char* why = "";
    std::string source;
    const bool have = FingerAnchor(h, side, in, oldW, newW, upm, &joint, &source, &why);
    if (have) {
        AcquireSRWLockExclusive(&g_bowAimLock);
        g_anchorWho = source;
        ReleaseSRWLockExclusive(&g_bowAimLock);
    }
    float eased;
    AcquireSRWLockExclusive(&g_bowAimLock);
    a.blend = g_anchorBlend.StepSparse(drawing && have, GetTickCount64(), in.bowAimBlendMs);
    eased = g_anchorBlend.Eased();
    ReleaseSRWLockExclusive(&g_bowAimLock);
    if (!have) { a.why = why; return a; }
    const V3 grip = ControllerPoint(h, side, in, newW, c, s);
    const bowhook::Vec p = bowhook::Lerp({grip.x, grip.y, grip.z}, {joint.x, joint.y, joint.z}, eased);
    a.point = {p.x, p.y, p.z};
    a.side = side;
    a.cm = Len(Sub(a.point, grip)) / upm * 100.0f;
    a.ok = a.blend > 0.0f;
    a.why = a.ok ? "the string is held at the fingers' first joints" : "the string is not drawn";
    if (a.ok) {   // in the hands' frame too: the draw's aim line starts where the arrow does
        const bowdraw::Vec inHands = bowtouch::ModelToHands({a.point.x, a.point.y, a.point.z}, {in.handOffset[0], in.handOffset[1], in.handOffset[2]},
                                                              {head.x, head.y, head.z}, upm, c, s);
        if (bowdraw::Finite(inHands)) {
            AcquireSRWLockExclusive(&g_bowAimLock);
            g_anchorHands[0] = inHands.x; g_anchorHands[1] = inHands.y; g_anchorHands[2] = inHands.z;
            g_anchorAt = GetTickCount64();
            ReleaseSRWLockExclusive(&g_bowAimLock);
        }
    }
    return a;
}

void AimBow(const Hero& h, const Input& in, const std::vector<Mat>& oldW, const std::vector<Mat>& baseW, std::vector<Mat>& newW, float c, float s, bool turn, int n,
            const StringAnchor& anchor) {
    const ULONGLONG now = GetTickCount64();
    BowAimStat st;
    st.on = in.bowAim;
    st.at = now;
    auto publish = [&] { AcquireSRWLockExclusive(&g_bowAimLock); g_aimStat = st; ReleaseSRWLockExclusive(&g_bowAimLock); };
    // The wrist the bow hangs on: its attach bone when the game shrinks the other's, else (a quiver in the other slot shows both: the
    // log said "R 1.00 L 1.00" and the aim never found a bow) the hand the draw detector and vrcam hold the bow in.
    float sizes[2];
    const bowaim::BowWrist wrist = BowWristNow(h, in, oldW, sizes);
    const int bowSide = wrist.side;
    {
        char who[160];
        snprintf(who, sizeof who, "%s wrist, told by %s; attach bones R %.2f L %.2f", bowSide == 0 ? "the right" : bowSide == 1 ? "the left" : "no", bowaim::SideByName(wrist.by), sizes[0], sizes[1]);
        st.who = who;
        char more[170];
        if (anchor.ok) snprintf(more, sizeof more, "; string held at the fingers' first joints, %.1f cm from the grip point (blend %.2f)", anchor.cm, anchor.blend);
        else snprintf(more, sizeof more, "; string held at the grip point (%s)", anchor.why);
        st.who += more;
    }
    const int boneSide = BowBoneSide(h, in, oldW, bowSide);
    if (bowSide < 0 || h.head < 0 || h.weapon[boneSide] < 0 || h.hand[bowSide] < 0) {
        st.why = bowSide < 0 ? "the wrist the bow hangs on is not known (the attach bones cannot tell, and neither the draw feed nor the aim hand)" : "the hero's wrist bones were not found";
        publish();
        return;
    }
    if ((in.handsValid & 3u) != 3u) { st.why = "a controller is not tracked"; publish(); return; }
    float af[3], au[3], as[3];
    if (!BowAxesNow(af, au, as)) { st.why = "the bow's own axes are not read yet (its rig is not a known layout, or not the hero's)"; publish(); return; }
    const int wb = h.weapon[boneSide], k = h.hand[bowSide];   // (the bone the mesh hangs on; the wrist the bow is held by)
    auto rig = [](V3 v) { return bowrig::V3{v.x, v.y, v.z}; };
    auto back = [](bowrig::V3 v) { return V3{v.x, v.y, v.z}; };
    auto asRig = [](const Mat& m) { bowrig::Mat r; memcpy(r.m, m.m, sizeof r.m); return r; };
    const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
    const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
    // [bow] string_anchor: the string hand's point is the fingers' first joints (eased with the draw), not the grip point - the arrow's line and
    // the bow's turn run from where the string is held
    const bool anchored = anchor.ok && anchor.side == (bowSide ^ 1);
    const V3 stringHand = anchored ? anchor.point : ControllerPoint(h, bowSide ^ 1, in, newW, c, s), bowHand = ControllerPoint(h, bowSide, in, newW, c, s);
    bool drawing = false;
    BowFeedSide(&drawing);   // the draw detector's word: the string is taken
    drawing = drawing || (in.bowRigGrip && in.grip[bowSide ^ 1] > 0.6f);
    float blend;
    AcquireSRWLockExclusive(&g_bowAimLock);
    blend = g_aimBlend.Step(in.bowAim && drawing, now, in.bowAimBlendMs);
    ReleaseSRWLockExclusive(&g_bowAimLock);
    st.blend = blend;

    const Mat oldAttach = newW[wb];
    const bowaim::Frame cur = bowaim::FrameOf(asRig(oldAttach), {af[0], af[1], af[2]}, {au[0], au[1], au[2]}, {as[0], as[1], as[2]});
    const bowaim::Roll rollMode = in.bowAimRoll == 1 ? bowaim::Roll::Upright : bowaim::Roll::Follow;
    // [bow] arrow_rest_*: where the arrow passes the bow hand is a point fixed in the hand's own frame (x along the bow, y across, z up, cm
    // from the controller's reference point), and the aim - the line the bow is turned along and the arrow lies on - runs from the string
    // hand THROUGH it; it comes in and goes out with the draw. All offsets 0: the line through the reference point, as it was.
    bowrig::V3 offsetCur{0.0f, 0.0f, 0.0f};
    float restFade = 0.0f;
    bool restModel = false;
    if (in.bowRest) {
        float ra[3] = {};
        AcquireSRWLockExclusive(&g_bowAimLock);
        restFade = g_restBlend.StepSparse(drawing, now, in.bowAimBlendMs);
        restModel = in.bowRestModel && g_restAttachAt && now - g_restAttachAt < 500;
        if (restModel) memcpy(ra, g_restAttach, sizeof ra);
        ReleaseSRWLockExclusive(&g_bowAimLock);
        // [bow] arrow_rest_model: the bow model's own rest - where its arrow passes the riser - carried by the attach bone to where the bow hangs now, as an
        // offset from the hand's point (SolveRest turns it with the bow); else the point the arrow_rest_*_cm offsets set in the hand's frame
        const bowrig::V3 fromModel = restModel ? bowrig::Point(asRig(oldAttach), {ra[0], ra[1], ra[2]}) - rig(bowHand) : bowrig::V3{};
        restModel = restModel && bowrig::Finite(fromModel) && bowrig::Length(fromModel) < 0.5f * upm;   // (a rest half a metre off the hand is no rest)
        offsetCur = (restModel ? fromModel : bowaim::OffsetIn(cur, in.bowRestCm[0], in.bowRestCm[1], in.bowRestCm[2]) * (0.01f * upm)) * restFade;
    }
    const bowaim::RestSolution rest = bowaim::SolveRest(rig(stringHand), rig(bowHand), cur, offsetCur, in.bowAim, rollMode, {0.0f, 1.0f, 0.0f}, blend, 0.05f * upm);   // hands under 5 cm apart give no line
    const bool haveAim = rest.ok;
    const bowrig::V3 aim = rest.aim;
    if (in.bowRest && haveAim) {   // the rest point, for the bow's own hook (the arrow's line), the draw's aim and the debug dot
        const bowdraw::Vec inHands = bowtouch::ModelToHands({rest.point.x, rest.point.y, rest.point.z}, {in.handOffset[0], in.handOffset[1], in.handOffset[2]},
                                                              {head.x, head.y, head.z}, upm, c, s);
        AcquireSRWLockExclusive(&g_bowAimLock);
        g_restModel[0] = rest.point.x; g_restModel[1] = rest.point.y; g_restModel[2] = rest.point.z;
        g_restHands[0] = inHands.x; g_restHands[1] = inHands.y; g_restHands[2] = inHands.z;
        g_restAt = now;
        g_restResidual = rest.residualDeg; g_restSteps = rest.steps; g_restSettled = rest.settled;
        ReleaseSRWLockExclusive(&g_bowAimLock);
        char more[160];
        if (restModel)
            snprintf(more, sizeof more, "; arrow rest: the bow model's own, %.1f cm from the hand's point, %s in %d steps (%.2f deg)",
                     bowrig::Length(offsetCur) / std::max(restFade, 1e-3f) / upm * 100.0f, rest.settled ? "settled" : "NOT settled", rest.steps, rest.residualDeg);
        else
            snprintf(more, sizeof more, "; arrow rest %.1f %.1f %.1f cm, %s in %d steps (%.2f deg)", in.bowRestCm[0], in.bowRestCm[1], in.bowRestCm[2], rest.settled ? "settled" : "NOT settled", rest.steps, rest.residualDeg);
        st.who += more;
    }
    st.raw = haveAim ? bowaim::AngleDeg(cur.forward, aim) : 0.0f;
    st.why = !in.bowAim ? "off (only the bow's axis and the arrow's rest point are read, for the debug and the arrow)" : !haveAim ? "the hands are too near each other to give a line" :
             !drawing && blend <= 0.0f ? "the string is not taken: the bow is as the controller holds it" : "turning the bow along the hands";
    if (in.bowAim && haveAim && blend > 0.0f) {
        Wrist wr = PlanWrist(h, bowSide, baseW, in, c, s);
        if (!wr.ok) {
            st.why = "the wrist is not turned by the controller ([hands] wrist is off)";
        } else {
            const bowrig::Mat delta = bowaim::AimTurn(cur, aim, rollMode, {0.0f, 1.0f, 0.0f}, blend);
            bowrig::V3 point = rig(wr.point), along = rig(wr.along);
            bowaim::TurnPlan(&point, &along, delta);
            wr.point = back(point);
            wr.along = back(along);
            // The palm stays at the controller; the wrist is half a hand behind it, along the turned hand. The arm is solved again.
            const V3 target = Sub(bowHand, Scale(wr.point, 0.5f * wr.reach));
            const Mat oldHand = newW[k];
            Reach(h, bowSide, target, turn, c, s, baseW, newW);
            ApplyWrist(newW[k], wr);
            std::vector<char> fromHand(n, 0);
            fromHand[k] = 1;
            Propagate(h, baseW, newW, fromHand, k);
            // The attach bone keeps its place on the hand (the grip, [weapon_bow]'s turn: whatever put it there), and what hangs on it follows.
            newW[wb] = Mul(Mul(oldAttach, InvAffine(oldHand)), newW[k]);
            std::vector<char> fromWeapon(n, 0);
            fromWeapon[wb] = 1;
            Propagate(h, oldW, newW, fromWeapon, wb);
        }
    }
    // The bow's forward axis as it stands now, for the debug and the log.
    const bowaim::Frame now2 = bowaim::FrameOf(asRig(newW[wb]), {af[0], af[1], af[2]}, {au[0], au[1], au[2]}, {as[0], as[1], as[2]});
    st.angle = haveAim ? bowaim::AngleDeg(now2.forward, aim) : 0.0f;
    float axis[3];
    FromModel(back(now2.forward), c, s, axis);
    // [bow] arrow_rest_clear: the arrow clear of the hand and the forearm. The bow arm as the pose has just left it (the wrist and the elbow
    // after the turn) is a few capsules; where the arrow's line, through the rest point, comes nearer to one than its radius, the nock and the
    // arrow are lifted together, up the bow's displayed limb axis, by as little as clears it (vr/bow_aim.h ClearanceLift) - and no more than
    // 8 cm. Smoothed over a few frames so that it does not flicker between two answers. Only the pose of the arrow and the nock.
    if (in.bowRest && haveAim) {
        float lift = 0.0f;
        if (in.bowRestClear && restFade > 0.0f && h.lwr[bowSide] >= 0) {
            const V3 elbow = Pos(newW[h.lwr[bowSide]]), wrist = Pos(newW[k]);
            const bowaim::Capsule arm[2] = {
                {rig(bowHand), rig(wrist), 0.050f * upm, 0.050f * upm},                 // the fist and the back of the hand
                {rig(wrist), rig(elbow), 0.042f * upm, 0.052f * upm},                   // the wrist and the forearm, thicker toward the elbow
            };
            const bowaim::ClearLift want = bowaim::ClearanceLift(rig(stringHand), rest.point, now2.up, 0.35f * upm, arm, 2, 0.006f * upm, 0.008f * upm, 0.08f * upm, 0.002f * upm);
            lift = want.lift * restFade;
            char more[100];
            snprintf(more, sizeof more, "; arrow lifted %.1f cm to clear the arm (%.1f cm from it before, %s)", want.lift / upm * 100.0f, want.before / upm * 100.0f, want.cleared ? "clear" : "NOT clear");
            st.who += more;
        }
        AcquireSRWLockExclusive(&g_bowAimLock);
        lift = g_restLiftSmooth.Step(lift, now, 50.0f);
        const bowrig::V3 up = now2.up;
        g_restLift[0] = up.x * lift; g_restLift[1] = up.y * lift; g_restLift[2] = up.z * lift;
        ReleaseSRWLockExclusive(&g_bowAimLock);
    }
    AcquireSRWLockExclusive(&g_bowAimLock);
    g_aimStat = st;
    memcpy(g_aimAxis, axis, sizeof axis);
    g_aimAxisAt = now;
    ReleaseSRWLockExclusive(&g_bowAimLock);
}
#endif

#if D2RVR_FIRST_PERSON
// ---- [flail] chain: the flail's head swings free on its chain (vr/flail_chain.h) ----
// The hero's flail (Flail, Knout, Scourge, Khalim's Flail and Will) has its chain as a line of bones; the game moves them only by its own
// keyframed animations, so with the arm on a controller the head stands stiff against the handle. Ticked (F4 only), the chain bones of the
// hero's OWN flail are posed from a small simulation each pose: the first link stays where the handle has it, the head hangs and swings with the
// hand's motion, and a hit by touch (meleetouch: FlailHit) kicks it back. It swings in the ROOM (OpenXR stage, metres), where the weight model's
// weapon lives too: the real hand, the real body's steps and turns swing it; the hero walking by the stick, a snap turn, the game's camera do not. Only those bones' matrices of this one skeleton; the
// game's attack, its reach and rolls, the handle and every other weapon are not touched. Unticked or any other view: nothing runs, nothing is
// written - the game's pose of the chain, as before.
static_assert(sizeof(flailchain::Mat) == sizeof(Mat), "the chain's matrices are the game's bone matrices");

std::atomic<bool> g_flailOn{false};                  // Set(): [flail] chain with the arms on the controllers (vrcam: F4 only)
std::atomic<float> g_flailDamping{0.6f}, g_flailSwing{1.0f}, g_flailHit{1.0f}, g_flailWeight{1.0f};
std::atomic<uint32_t> g_flailHits[2];                // FlailHit: a hit by touch of the weapon in that hand slot (0 right, 1 left)
std::atomic<const char*> g_flailWhy{"no flail posed yet"};
std::atomic<ULONGLONG> g_flailPosedAt{0};
SRWLOCK g_flailNoteLock = SRWLOCK_INIT;
std::string g_flailNote;                             // the models learned, once each, for vrcam's log

// What this pose of the hero gives the flail's: the hands' attach bones as ours left them, his model-to-world, and his model space in the room.
struct FlailHands {
    ULONGLONG at = 0;
    uintptr_t hero = 0;
    uint32_t gen = 0;
    bool attachOk[2] = {};                           // [0] right, [1] left: the bone is there and not shrunk to a point (a hand holding nothing)
    Mat attach[2]{};
    Mat heroModel{};                                 // his TransformComponent's local matrix: model space -> the game's world (which wrist: PoseFlail)
    Mat toRoom{};                                    // model space -> the room (metres, y up): the hands' own mapping (ControllerPoint) run backwards,
                                                     // then the head's place and turn in the room (weight.cpp's HeadOf / ToRoom)
};
SRWLOCK g_flailLock = SRWLOCK_INIT;
FlailHands g_flailHands;

void NoteFlailHands(uintptr_t self, const Hero& h, const Input& in, const std::vector<Mat>& newW) {
    FlailHands f;
    HeroXform hx;
    if (h.head < 0 || !HeroTransform(&hx) || hx.self != self || (hx.parent & 0xFFFFFu) != 0xFFFFFu || !SafeCopy(f.heroModel.m, (const void*)hx.comp, sizeof f.heroModel.m)) return;
    PoseModel(f.heroModel);
    for (int side = 0; side < 2; ++side) {
        if (h.weapon[side] < 0) continue;
        f.attach[side] = newW[h.weapon[side]];
        f.attachOk[side] = Len(V3{f.attach[side].m[0], f.attach[side].m[1], f.attach[side].m[2]}) > 0.05f && boneguard::Finite(f.attach[side].m);
    }
    const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
    const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
    flailchain::Mat room;
    if (!flailchain::RoomFrame({head.x, head.y, head.z}, upm, cosf(in.yawInModel), sinf(in.yawInModel), {in.headRoom[0], in.headRoom[1], in.headRoom[2]},
                               in.headYawDeg, &room)) return;
    memcpy(f.toRoom.m, room.m, sizeof f.toRoom.m);
    f.at = GetTickCount64();
    f.hero = self;
    f.gen = HeroGen();
    AcquireSRWLockExclusive(&g_flailLock);
    g_flailHands = f;
    ReleaseSRWLockExclusive(&g_flailLock);
}

// A flail skeleton met: its chains learned from the bind pose, or why not. (Every skeleton of a flail's size gets an entry - most of them are
// monsters' - so the chains and their state hang off it only for a flail.)
struct FlailState {
    std::string model;
    int count = 0;
    int chains = 0;
    flailchain::ChainRig rig[flailchain::kMaxChains];
    flailchain::Sim sim[flailchain::kMaxChains];
    int lowest = 0, highest = -1;                    // the chains' bones span these indices (written as one block, the rest as the game had it)
    const char* why = nullptr;
    uint32_t entity = 0;
    bool entityKnown = false;
    ULONGLONG lookupAt = 0;
    int side = -1;                                   // the wrist it hangs on (0 right, 1 left), as last read
    uint32_t hitsSeen[2] = {};
    LARGE_INTEGER lastAt{};
    uint32_t gen = 0;
    ULONGLONG kickAt = 0;                            // the last hit's kick (the log's status holds it 0.4 s: vrcam reads it every 10 ms)
    flailchain::Head head;                           // the ball (hit by touch strikes with it): measured per model
    int strikes = 0;                                 // the chain whose ball strikes (Khalim's: the middle one)
};
// The striking ball of the hero's flail as the last pose of it left it, in the ITEM's model space with the item's place on its attach bone: the
// hero's pose (PoseBody, PosedBlade::ball) carries it onto the attach bone as THAT pose has it, so the ball comes with the hand sample the
// touch matches it to (the chain's own motion is a frame old at most).
struct FlailBall {
    ULONGLONG at = 0;
    uint32_t gen = 0;
    int side = -1;                                   // the wrist slot it hangs on (0 right, 1 left)
    Mat itemLocal{};                                 // the item's transform against that attach bone
    V3 centre{}, dir{};                              // the ball's centre and the last link's direction (toward the ball)
    float radius = 0.0f;                             // model units
    int more = 0;                                    // Khalim's: the other chains' balls (each strikes too)
    V3 moreCentre[PosedBlade::kMoreBalls]{}, moreDir[PosedBlade::kMoreBalls]{};
};
SRWLOCK g_flailBallLock = SRWLOCK_INIT;
FlailBall g_flailBall;
struct FlailEntry {
    uintptr_t bones = 0;
    std::unique_ptr<FlailState> flail;               // a flail's: else none
};
SRWLOCK g_flailRigLock = SRWLOCK_INIT;
std::map<std::pair<uintptr_t, uintptr_t>, FlailEntry> g_flails;

void NoteFlail(const std::string& line) {
    AcquireSRWLockExclusive(&g_flailNoteLock);
    if (g_flailNote.size() < 1500) g_flailNote += (g_flailNote.empty() ? "" : " | ") + line;
    ReleaseSRWLockExclusive(&g_flailNoteLock);
}

void LearnFlail(uintptr_t self, uintptr_t bones, int count, FlailEntry* entry) {
    uintptr_t object = 0, name = 0;
    Get(self, &object); Get(object + 8, &name);
    const std::string path = Str(name);
    if (!flailchain::IsFlailModel(path)) return;
    entry->flail = std::make_unique<FlailState>();
    FlailState* e = entry->flail.get();
    e->model = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    e->count = count;
    std::string names[flailchain::kMaxBones];
    std::string_view views[flailchain::kMaxBones];
    int parents[flailchain::kMaxBones] = {};
    flailchain::Mat bind[flailchain::kMaxBones];
    bool read = count >= 2 && count <= flailchain::kMaxBones;
    for (int i = 0; i < count && read; ++i) {
        uintptr_t nm = 0;
        const uintptr_t at = bones + (uintptr_t)i * 0xA4;
        Mat inverse;
        read = Get(at, &nm) && Get(at + 8, &parents[i]) && SafeCopy(inverse.m, (const void*)(at + 0x50), sizeof inverse.m);   // the bind pose: the inverse of the world matrix at +0x50
        names[i] = read ? Str(nm) : std::string();
        views[i] = names[i];
        if (read) { const Mat world = InvAffine(inverse); memcpy(bind[i].m, world.m, sizeof world.m); }
    }
    if (!read) { e->why = "a flail whose bones do not read"; NoteFlail("flail chain: " + e->model + "'s bones do not read"); return; }
    flailchain::Layout layout;
    const char* why = nullptr;
    if (!flailchain::FindChains(views, parents, count, &layout, &why)) {
        e->why = "a flail with no chain of bones - the game's animation";
        NoteFlail("flail chain: " + e->model + ", " + std::to_string(count) + " bones: " + (why ? why : "no chain"));
        return;
    }
    std::string text;
    for (int c = 0; c < layout.chains; ++c) {
        flailchain::ChainRig r;
        if (!flailchain::Learn(layout.chain[c], parents, bind, count, &r, &why)) {
            text += std::string(text.empty() ? "" : "; ") + "a chain from " + names[layout.chain[c].bone[0]] + " refused (" + (why ? why : "?") + ")";
            continue;
        }
        e->rig[e->chains++] = r;
        for (int k = 0; k < r.bones; ++k) { e->lowest = e->highest < 0 ? r.bone[k] : std::min(e->lowest, r.bone[k]); e->highest = std::max(e->highest, r.bone[k]); }
        char b[200];
        snprintf(b, sizeof b, "%s%s .. %s (%d bones, %d places, %.2f long) off %s", text.empty() ? "" : "; ", names[r.bone[0]].c_str(), names[r.bone[r.bones - 1]].c_str(),
                 r.bones, r.particles, r.bindLength, names[r.parent].c_str());
        text += b;
    }
    if (!e->chains) e->why = "a flail whose chains did not learn - the game's animation";
    e->head = flailchain::HeadOf(e->model);
    e->strikes = flailchain::StrikingChain(e->rig, e->chains, views, count);
    if (e->chains) {
        char b[160];
        snprintf(b, sizeof b, "; the ball that strikes: past %s, %.3f along the last link, radius %.3f (model units)", names[e->rig[e->strikes].bone[e->rig[e->strikes].bones - 1]].c_str(),
                 e->head.along, e->head.radius);
        text += e->head.ok ? b : "; no ball known for this model (hit by touch keeps the handle's blade)";
    }
    NoteFlail("flail chain: " + e->model + ", " + std::to_string(count) + " bones: " + std::to_string(e->chains) + " chain(s): " + text);
}

void PoseFlail(uintptr_t self) {
    if (!g_flailOn.load(std::memory_order_relaxed)) return;
    uintptr_t model = 0, skeleton = 0, bones = 0;
    int count = 0;
    if (!Get(self + 0x08, &model) || !Get(model + 0x08, &skeleton) || !Get(skeleton + 8, &count) || count < 8 || count > flailchain::kMaxBones ||
        !Get(skeleton + 0xC, &bones) || !bones) return;   // anything but a skeleton the size of a flail's leaves here
    const ULONGLONG now = GetTickCount64();
    const auto key = std::make_pair(self, skeleton);
    if (KnownNot(g_flailRigLock, g_flails, key, bones, [](const FlailEntry& e) { return !e.flail; })) return;   // most rigs: not a flail, told under the shared lock
    AcquireSRWLockExclusive(&g_flailRigLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_flailRigLock); } } unlock;
    auto it = g_flails.find(key);
    if (it == g_flails.end() || it->second.bones != bones) {
        FlailEntry e;
        e.bones = bones;
        LearnFlail(self, bones, count, &e);
        if (g_flails.size() > 4096) std::erase_if(g_flails, [](const auto& f) { return !f.second.flail; });   // full: the flails are kept
        if (g_flails.size() > 4096) g_flails.clear();
        it = g_flails.insert_or_assign(key, std::move(e)).first;
    }
    if (!it->second.flail) return;
    FlailState& held = *it->second.flail;
    auto why = [&](const char* w) { g_flailWhy.store(w); };
    if (!held.chains) { why(held.why ? held.why : "a flail with no chain"); return; }
    HeroXform hero;
    if (!HeroTransform(&hero)) { why("the hero's transform is not known"); return; }
    // Whose is it? Only the hero's own flail: its entity hangs off his (an NPC's, a dropped one, another player's never does).
    Mat itemLocal, itemWorld;
    bool haveWorld = false;
    {
        AcquireSRWLockExclusive(&ecs::g_lock);
        struct EcsUnlock { ~EcsUnlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } ecsUnlock;
        uintptr_t existing = 0;
        bool found = held.entityKnown && ecs::SkeletonOf(held.entity, &existing) && existing == self;
        if (!found) {
            held.entityKnown = false;
            if (held.lookupAt && now - held.lookupAt < 500) return;   // looked for twice a second at most
            held.lookupAt = now;
            found = held.entityKnown = ecs::EntityOf(self, &held.entity);
            if (!found) { why("the flail's entity is not found yet"); return; }
        }
        uint32_t index = 0, parent = 0;
        uintptr_t components = 0;
        if (!ecs::Dense(ecs::g_xs, held.entity, &index) || !Get(ecs::g_xs + 0x38, &components) || !components ||
            !Get(components + (uintptr_t)index * kTransformSize + 0xC0, &parent)) { why("the flail entity's transform is not found"); return; }
        if (!flailchain::Wanted(true, true, (parent & 0xFFFFF) == (hero.entity & 0xFFFFF))) return;   // not the hero's: the game's pose, says nothing
        if (!SafeCopy(itemLocal.m, (const void*)(components + (uintptr_t)index * kTransformSize), sizeof itemLocal.m)) { why("the flail entity's transform did not read"); return; }
        haveWorld = SafeCopy(itemWorld.m, (const void*)(components + (uintptr_t)index * kTransformSize + 0x40), sizeof itemWorld.m) && boneguard::Finite(itemWorld.m);
    }
    FlailHands hands;
    AcquireSRWLockShared(&g_flailLock); hands = g_flailHands; ReleaseSRWLockShared(&g_flailLock);
    if (!hands.at || now - hands.at >= 150 || hands.hero != g_heroSelf.load() || hands.gen != HeroGen()) {
        why("the hero's pose has not placed the hands lately");
        return;
    }
    // The wrist it hangs on: of the attach bones shown, the one that puts the item where the game has it (its world matrix); kept unless the
    // other is clearly nearer. Unreadable: the left-hand model (flail_lh) on the left, any other on the right.
    {
        float d[2] = {1e30f, 1e30f};
        for (int side = 0; side < 2; ++side)
            if (hands.attachOk[side] && haveWorld) d[side] = Len(Sub(Pos(Mul(Mul(itemLocal, hands.attach[side]), hands.heroModel)), Pos(itemWorld)));
        int side = held.side;
        if (d[0] < 1e29f || d[1] < 1e29f) {
            const int nearer = d[0] <= d[1] ? 0 : 1;
            if (side < 0 || !hands.attachOk[side] || d[nearer] * 2.0f < d[side]) side = nearer;
        } else {
            side = held.model.find("_lh") != std::string::npos ? 1 : 0;
        }
        if (side < 0 || !hands.attachOk[side]) { why("the wrist the flail hangs on is not shown"); return; }
        held.side = side;
    }
    // item model -> the room: the simulation's frame (metres, y up)
    const Mat toRoom = Mul(Mul(itemLocal, hands.attach[held.side]), hands.toRoom);
    if (!boneguard::Finite(toRoom.m)) { why("the flail's place in the room is not numbers"); return; }
    if (held.gen != HeroGen()) {   // a new hero model: started again where the game has it
        held.gen = HeroGen();
        for (int c = 0; c < held.chains; ++c) held.sim[c].live = false;
    }
    flailchain::Mat toSim, fromSim;
    memcpy(toSim.m, toRoom.m, sizeof toSim.m);
    if (!flailchain::Inverse(toSim, &fromSim)) { why("the flail's frame has no inverse"); return; }
    // the time since the last pose of it (the first, or one after a pause, starts the chain where the game has it)
    LARGE_INTEGER qpc, freq;
    QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&freq);
    const float dt = held.lastAt.QuadPart ? (float)((double)(qpc.QuadPart - held.lastAt.QuadPart) / (double)freq.QuadPart) : 1.0f;
    held.lastAt = qpc;
    // a hit by touch of the weapon in its hand slot since the last pose
    const uint32_t hits = g_flailHits[held.side].load();
    const bool kick = hits != held.hitsSeen[held.side] && dt <= 0.25f;
    held.hitsSeen[0] = g_flailHits[0].load(); held.hitsSeen[1] = g_flailHits[1].load();
    flailchain::Params p;
    p.damping = g_flailDamping.load();
    p.swing = g_flailSwing.load();
    p.hit = g_flailHit.load();
    p.weight = g_flailWeight.load();
    const flailchain::V3 gravity{0.0f, -9.81f, 0.0f};   // the room's y is up
    uintptr_t world = 0, matrices = 0;
    int worldCount = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &worldCount) || worldCount != held.count || !Get(world + 4, &matrices) || !matrices) { why("its pose matrices are not the skeleton's"); return; }
    flailchain::Mat pose[flailchain::kMaxBones];
    if (!SafeCopy(pose, (const void*)matrices, sizeof(Mat) * (size_t)held.count)) { why("its pose matrices did not read"); return; }
    bool started = false;
    for (int c = 0; c < held.chains; ++c)
        started = flailchain::Drive(held.rig[c], held.sim[c], toSim, fromSim, dt, gravity, 1.0f, kick, p, pose) == flailchain::Done::Started || started;   // a 1 m jump: started again
    if (held.chains > 1 && held.head.ok) {   // Khalim's three: the balls kept apart - never one lump (flailchain::Separate), the chains posed again where they are
        flailchain::V3 restHeads[flailchain::kMaxChains];
        for (int c = 0; c < held.chains; ++c) restHeads[c] = flailchain::RestHead(held.rig[c], pose, toSim);
        const float minDist = 2.0f * held.head.radius * flailchain::Length(flailchain::Row(toSim, 0)) * 0.95f;
        if (flailchain::Separate(held.sim, held.chains, minDist, restHeads, p.iterations, held.head.along * flailchain::Length(flailchain::Row(toSim, 0))) > 0)
            for (int c = 0; c < held.chains; ++c) flailchain::Repose(held.rig[c], held.sim[c], fromSim, pose);
    }
    for (int i = held.lowest; i <= held.highest; ++i)
        if (!boneguard::Finite(pose[i].m)) { why("the chain's pose did not come out as numbers"); for (int c = 0; c < held.chains; ++c) held.sim[c].live = false; return; }
    if (held.highest < held.lowest ||
        !SafeWrite((void*)(matrices + (size_t)held.lowest * sizeof(Mat)), pose + held.lowest, (size_t)(held.highest - held.lowest + 1) * sizeof(Mat))) {
        why("the chain's pose could not be written");
        return;
    }
    g_flailPosedAt.store(now);
    {   // hit by touch: the ball that strikes, where this pose has it
        FlailBall b;
        flailchain::V3 centre, dir;
        if (flailchain::Ball(held.rig[held.strikes], pose, held.head, &centre, &dir)) {
            b.at = now; b.gen = HeroGen(); b.side = held.side; b.itemLocal = itemLocal;
            b.centre = {centre.x, centre.y, centre.z}; b.dir = {dir.x, dir.y, dir.z}; b.radius = held.head.radius;
            for (int c = 0; c < held.chains && b.more < PosedBlade::kMoreBalls; ++c)   // ... and every other chain's ball (Khalim's three: each strikes)
                if (c != held.strikes && flailchain::Ball(held.rig[c], pose, held.head, &centre, &dir)) {
                    b.moreCentre[b.more] = {centre.x, centre.y, centre.z};
                    b.moreDir[b.more] = {dir.x, dir.y, dir.z};
                    ++b.more;
                }
        }
        AcquireSRWLockExclusive(&g_flailBallLock); g_flailBall = b; ReleaseSRWLockExclusive(&g_flailBallLock);
    }
    if (kick) held.kickAt = now;
    why(started ? "posed - the chain started where the game has it" : held.kickAt && now - held.kickAt < 400 ? "posed - the head kicked back by a hit" : "posed - swinging free");
}
#endif

#if D2RVR_FIRST_PERSON
// ---- [shield] heads_hang: the Necromancer's head hangs from its cord under the hand (vr/head_hang.h) ----
// A head in the left slot is a "shield" to the game's data (ItemTypes head), so [shield] forearm strapped it to the forearm, and the game's own
// pose of it - made for the arm the game animates - held it stiff at the hand's angle. Ticked (F4 only): PoseBody leaves its bones with the hand
// (HoldHead: not strapped), and the head's OWN skeleton is turned here, as a whole, about the point the game hangs it from (the model's origin:
// the cord's knot, the handle), so that it hangs under gravity, swings after the hand and twists after its turn. It swings in the ROOM, as the
// flail's chain does. Only the bone matrices of the hero's own head; the item, its block and its stats are the game's. Unticked or any other
// view: nothing runs, nothing is written - as before.
std::atomic<bool> g_headOn{false};                   // Set(): [shield] heads_hang with the arms on the controllers (vrcam: F4 and a head in a hand slot)
std::atomic<const char*> g_headWhy{"no head posed yet"};
std::atomic<ULONGLONG> g_headSeenAt{0};              // the last pose of any head skeleton the hook met (HeadStatus: none lately is a reason too)
SRWLOCK g_headNoteLock = SRWLOCK_INIT;
std::string g_headNote;                              // the head models learned, once each, for vrcam's log

// The bones the head may hang on - L_weapon_attach, shield_attach, R_weapon_attach - as this pose of the hero left them, his model-to-world and
// his model space in the room (as the flail's FlailHands).
constexpr int kHeadBones = 3;
struct HeadHands {
    ULONGLONG at = 0;
    uintptr_t hero = 0;
    uint32_t gen = 0;
    bool ok[kHeadBones] = {};                        // the bone is there and not shrunk to a point
    Mat attach[kHeadBones]{};
    Mat heroModel{};
    Mat toRoom{};
};
SRWLOCK g_headLock = SRWLOCK_INIT;
HeadHands g_headHands;

void NoteHeadHands(uintptr_t self, const Hero& h, const Input& in, const std::vector<Mat>& newW) {
    HeadHands f;
    HeroXform hx;
    if (h.head < 0 || !HeroTransform(&hx) || hx.self != self || (hx.parent & 0xFFFFFu) != 0xFFFFFu || !SafeCopy(f.heroModel.m, (const void*)hx.comp, sizeof f.heroModel.m)) return;
    PoseModel(f.heroModel);
    const int bones[kHeadBones] = {h.weapon[1], h.shield, h.weapon[0]};
    for (int b = 0; b < kHeadBones; ++b) {
        if (bones[b] < 0 || bones[b] >= (int)newW.size()) continue;
        f.attach[b] = newW[bones[b]];
        f.ok[b] = Len(V3{f.attach[b].m[0], f.attach[b].m[1], f.attach[b].m[2]}) > 0.05f && boneguard::Finite(f.attach[b].m);
    }
    const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
    const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
    flailchain::Mat room;
    if (!flailchain::RoomFrame({head.x, head.y, head.z}, upm, cosf(in.yawInModel), sinf(in.yawInModel), {in.headRoom[0], in.headRoom[1], in.headRoom[2]},
                               in.headYawDeg, &room)) return;
    memcpy(f.toRoom.m, room.m, sizeof f.toRoom.m);
    f.at = GetTickCount64();
    f.hero = self;
    f.gen = HeroGen();
    AcquireSRWLockExclusive(&g_headLock);
    g_headHands = f;
    ReleaseSRWLockExclusive(&g_headLock);
}

// PoseBody: the head's bones (the left attach bone and shield_attach) with the hand, not strapped to the forearm. The left attach bone is under the
// wrist and already there; shield_attach hangs under the forearm in the rig, so it is carried by the wrist as the game's animation has it against it.
// A head the game hides (shrunk to a point: a weapon swap) stays the game's. True when the hand holds it.
bool HoldHead(const Hero& h, const std::vector<Mat>& oldW, std::vector<Mat>& newW) {
    const int k = h.hand[1];
    if (k < 0 || h.weapon[1] < 0) return false;
    const Mat anim = Mul(oldW[h.weapon[1]], InvAffine(oldW[k]));
    if (Len({anim.m[0], anim.m[1], anim.m[2]}) < 0.3f) return false;   // hidden by the game
    if (h.shield >= 0 && h.shield != h.weapon[1]) {
        const std::vector<Mat> before = newW;
        newW[h.shield] = Mul(Mul(oldW[h.shield], InvAffine(oldW[k])), newW[k]);
        std::vector<char> fromS(h.count, 0);
        fromS[h.shield] = 1;
        Propagate(h, before, newW, fromS, h.shield);
    }
    return true;
}

// A head skeleton met: where it hangs from, learned from its bind pose and its meshes' boxes, or why not.
struct HeadState {
    std::string model;
    int count = 0;
    headhang::Rig rig;
    headhang::State state;
    uint32_t entity = 0;
    bool entityKnown = false;
    ULONGLONG lookupAt = 0;
    int bone = -1;                                   // the hero's bone it hangs on (HeadHands' order), as last read
    LARGE_INTEGER lastAt{};
    uint32_t gen = 0;
    bool toldHung = false, toldNotHero = false;      // the log's notes, once each
    int cog = -1;                                    // its cog bone and that bone's bind pose (the frame it hangs by), -1 none
    Mat cogBind{};
    bool fromBones = false;                          // hung by its bones: the meshes' boxes did not read (asked again a few times: boxTries, boxAt)
    int boxTries = 0;
    ULONGLONG boxAt = 0;
};
struct HeadEntry {
    uintptr_t bones = 0;
    std::unique_ptr<HeadState> head;                 // a head's: else none
};
SRWLOCK g_headRigLock = SRWLOCK_INIT;
std::map<std::pair<uintptr_t, uintptr_t>, HeadEntry> g_heads;
constexpr int kHeadMaxBones = 64;   // the heads' skeletons have 8 (preserved) .. 24 (zombie)

void NoteHead(const std::string& line) {
    AcquireSRWLockExclusive(&g_headNoteLock);
    if (g_headNote.size() < 1500) g_headNote += (g_headNote.empty() ? "" : " | ") + line;
    ReleaseSRWLockExclusive(&g_headNoteLock);
}

// The head's meshes' boxes off the instance the hook poses (grannymesh::MeshBoxes) and the hang from them; not ok, with why, when none read.
headhang::Hang HangFromBoxes(uintptr_t self, int* got, const char** why) {
    grannymesh::Box boxes[16];
    uintptr_t instance = 0, skel = 0;
    auto rd = [](uintptr_t at, void* to, size_t n) { return SafeCopy(to, (const void*)at, n); };
    *why = "the hook's instance does not read";
    *got = Get(self + 0x08, &instance) && Get(instance + 0x08, &skel) ? grannymesh::MeshBoxes(rd, instance, skel, boxes, 16, why) : 0;
    headhang::Box hb[16];
    for (int i = 0; i < *got; ++i) hb[i] = {{boxes[i].min[0], boxes[i].min[1], boxes[i].min[2]}, {boxes[i].max[0], boxes[i].max[1], boxes[i].max[2]}};
    const headhang::Hang hang = headhang::HangOf(hb, *got);
    if (*got && !hang.ok) *why = "no mesh below its origin";
    return hang;
}
// The rig from a hang: by its cog when it has one (and its bind pose inverts), else by the model's own frame.
headhang::Rig HeadRig(const HeadState& e, const headhang::Hang& hang) {
    if (e.cog >= 0) {
        flailchain::Mat cb;
        memcpy(cb.m, e.cogBind.m, sizeof cb.m);
        const headhang::Rig r = headhang::RigOf(hang, e.cog, &cb);
        if (r.ok) return r;
    }
    return headhang::RigOf(hang, -1, nullptr);
}

// A head skeleton met: where it hangs from - its meshes' boxes if they read, else its own bones (in the game the boxes do not read: the
// skeleton is a .skeleton file's, whose model has no meshes) - and how. It always hangs: no way back to the game's stiff pose.
void LearnHead(uintptr_t self, uintptr_t bones, int count, HeadEntry* entry) {
    uintptr_t object = 0, name = 0;
    Get(self, &object); Get(object + 8, &name);
    const std::string path = Str(name);
    if (!headhang::IsHeadModel(path)) return;
    entry->head = std::make_unique<HeadState>();
    HeadState* e = entry->head.get();
    e->model = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    e->count = count;
    headhang::V3 places[kHeadMaxBones];
    int unread = 0;
    for (int i = 0; i < count; ++i) {
        uintptr_t nm = 0;
        const uintptr_t at = bones + (uintptr_t)i * 0xA4;
        Mat inverse;
        places[i] = {std::nanf(""), 0.0f, 0.0f};   // (a bone that does not read is left out of the hang)
        if (!Get(at, &nm) || !SafeCopy(inverse.m, (const void*)(at + 0x50), sizeof inverse.m)) { ++unread; continue; }
        const Mat bind = InvAffine(inverse);
        places[i] = {bind.m[12], bind.m[13], bind.m[14]};
        if (e->cog < 0 && _stricmp(Str(nm).c_str(), "cog") == 0) { e->cog = i; e->cogBind = bind; }
    }
    int got = 0;
    const char* boxWhy = nullptr;
    headhang::Hang hang = HangFromBoxes(self, &got, &boxWhy);
    e->fromBones = !hang.ok;
    if (e->fromBones) hang = headhang::HangOfBones(places, count);
    e->rig = HeadRig(*e, hang);
    e->boxAt = GetTickCount64();
    char b[400];
    snprintf(b, sizeof b, "head hang: %s, %d bones%s: hangs from its origin along %.3f %.3f %.3f (the model's), the cord %.3f units, by its %s - %s%s%s",
             e->model.c_str(), count, unread ? " (some do not read)" : "", hang.dir.x, hang.dir.y, hang.dir.z, hang.length,
             e->rig.carrier >= 0 ? "cog" : "own frame", e->fromBones ? "from its bones (its meshes' boxes: " : "from its meshes' boxes",
             e->fromBones ? (boxWhy ? boxWhy : "none") : "", e->fromBones ? "; asked again later)" : "");
    NoteHead(b);
}

void PoseHead(uintptr_t self) {
    if (!g_headOn.load(std::memory_order_relaxed)) return;
    uintptr_t model = 0, skeleton = 0, bones = 0;
    int count = 0;
    if (!Get(self + 0x08, &model) || !Get(model + 0x08, &skeleton) || !Get(skeleton + 8, &count) || count < 2 || count > kHeadMaxBones ||
        !Get(skeleton + 0xC, &bones) || !bones) return;
    const ULONGLONG now = GetTickCount64();
    const auto key = std::make_pair(self, skeleton);
    if (KnownNot(g_headRigLock, g_heads, key, bones, [](const HeadEntry& e) { return !e.head; })) return;   // most rigs: not a head, told under the shared lock
    AcquireSRWLockExclusive(&g_headRigLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_headRigLock); } } unlock;
    auto it = g_heads.find(key);
    if (it == g_heads.end() || it->second.bones != bones) {
        HeadEntry e;
        e.bones = bones;
        LearnHead(self, bones, count, &e);
        if (g_heads.size() > 4096) std::erase_if(g_heads, [](const auto& f) { return !f.second.head; });   // full: the heads are kept
        if (g_heads.size() > 4096) g_heads.clear();
        it = g_heads.insert_or_assign(key, std::move(e)).first;
    }
    if (!it->second.head) return;
    HeadState& held = *it->second.head;
    g_headSeenAt.store(now);
    auto why = [&](const char* w) { g_headWhy.store(w); };
    if (held.fromBones && held.boxTries < 5 && now - held.boxAt >= 2000) {   // hung by its bones: the meshes' boxes asked again, five times two seconds apart
        held.boxAt = now;
        ++held.boxTries;
        int got = 0;
        const char* boxWhy = nullptr;
        const headhang::Hang hang = HangFromBoxes(self, &got, &boxWhy);
        if (hang.ok) {
            held.rig = HeadRig(held, hang);
            held.fromBones = false;
            char b[260];
            snprintf(b, sizeof b, "head hang: %s - its meshes' boxes read now (%d): along %.3f %.3f %.3f, the cord %.3f units", held.model.c_str(), got, hang.dir.x, hang.dir.y, hang.dir.z, hang.length);
            NoteHead(b);
        }
    }
    if (!held.rig.ok) { why("a head that cannot hang (its rig did not learn)"); return; }
    HeroXform hero;
    if (!HeroTransform(&hero)) { why("the hero's transform is not known"); return; }
    // Whose is it? Only the hero's own head: its entity hangs off his (one on the ground, another player's never does).
    Mat itemLocal, itemWorld;
    bool haveWorld = false;
    {
        AcquireSRWLockExclusive(&ecs::g_lock);
        struct EcsUnlock { ~EcsUnlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } ecsUnlock;
        uintptr_t existing = 0;
        bool found = held.entityKnown && ecs::SkeletonOf(held.entity, &existing) && existing == self;
        if (!found) {
            held.entityKnown = false;
            if (held.lookupAt && now - held.lookupAt < 500) return;   // looked for twice a second at most
            held.lookupAt = now;
            found = held.entityKnown = ecs::EntityOf(self, &held.entity);
            if (!found) { why("the head's entity is not found yet"); return; }
        }
        uint32_t index = 0, parent = 0;
        uintptr_t components = 0;
        if (!ecs::Dense(ecs::g_xs, held.entity, &index) || !Get(ecs::g_xs + 0x38, &components) || !components ||
            !Get(components + (uintptr_t)index * kTransformSize + 0xC0, &parent)) { why("the head entity's transform is not found"); return; }
        if ((parent & 0xFFFFF) != (hero.entity & 0xFFFFF)) {   // not the hero's: the game's pose (the status says nothing: the hero's own may be posed too)
            if (!held.toldNotHero) {
                held.toldNotHero = true;
                char b[200];
                snprintf(b, sizeof b, "head hang: a %s skeleton whose entity hangs off entity %05X, not the hero's (%05X) - left as the game poses it (on the ground, or another's)",
                         held.model.c_str(), parent & 0xFFFFFu, hero.entity & 0xFFFFFu);
                NoteHead(b);
            }
            return;
        }
        if (!SafeCopy(itemLocal.m, (const void*)(components + (uintptr_t)index * kTransformSize), sizeof itemLocal.m)) { why("the head entity's transform did not read"); return; }
        haveWorld = SafeCopy(itemWorld.m, (const void*)(components + (uintptr_t)index * kTransformSize + 0x40), sizeof itemWorld.m) && boneguard::Finite(itemWorld.m);
    }
    HeadHands hands;
    AcquireSRWLockShared(&g_headLock); hands = g_headHands; ReleaseSRWLockShared(&g_headLock);
    if (!hands.at || now - hands.at >= 150 || hands.hero != g_heroSelf.load() || hands.gen != HeroGen()) { why("the hero's pose has not placed the hands lately"); return; }
    // The bone it hangs on: of the ones shown, the one that puts the item where the game has it (its world matrix); kept unless another is
    // clearly nearer. Unreadable: the left attach bone.
    {
        float d[kHeadBones];
        int nearest = -1;
        for (int b = 0; b < kHeadBones; ++b) {
            d[b] = hands.ok[b] && haveWorld ? Len(Sub(Pos(Mul(Mul(itemLocal, hands.attach[b]), hands.heroModel)), Pos(itemWorld))) : 1e30f;
            if (d[b] < 1e29f && (nearest < 0 || d[b] < d[nearest])) nearest = b;
        }
        int bone = held.bone;
        if (nearest >= 0) { if (bone < 0 || !hands.ok[bone] || d[nearest] * 2.0f < d[bone]) bone = nearest; }
        else bone = 0;
        if (bone < 0 || !hands.ok[bone]) { why("the bone the head hangs on is not shown"); return; }
        held.bone = bone;
    }
    // the head's model -> the room: the simulation's frame (metres, y up)
    const Mat toRoom = Mul(Mul(itemLocal, hands.attach[held.bone]), hands.toRoom);
    if (!boneguard::Finite(toRoom.m)) { why("the head's place in the room is not numbers"); return; }
    if (held.gen != HeroGen()) { held.gen = HeroGen(); held.state.live = false; }   // a new hero model: started again
    flailchain::Mat toSim, fromSim;
    memcpy(toSim.m, toRoom.m, sizeof toSim.m);
    if (!flailchain::Inverse(toSim, &fromSim)) { why("the head's frame has no inverse"); return; }
    LARGE_INTEGER qpc, freq;
    QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&freq);
    const float dt = held.lastAt.QuadPart ? (float)((double)(qpc.QuadPart - held.lastAt.QuadPart) / (double)freq.QuadPart) : 1.0f;
    held.lastAt = qpc;
    uintptr_t world = 0, matrices = 0;
    int worldCount = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &worldCount) || worldCount != held.count || !Get(world + 4, &matrices) || !matrices) { why("its pose matrices are not the skeleton's"); return; }
    flailchain::Mat pose[kHeadMaxBones];
    if (!SafeCopy(pose, (const void*)matrices, sizeof(Mat) * (size_t)held.count)) { why("its pose matrices did not read"); return; }
    const headhang::Done done = headhang::Drive(held.rig, held.state, toSim, fromSim, dt, {0.0f, -9.81f, 0.0f}, 1.0f, headhang::Params{}, pose, held.count);
    if (!held.state.live) { why("the head's hang did not start (its frame is not square)"); return; }
    for (int i = 0; i < held.count; ++i)
        if (!boneguard::Finite(pose[i].m)) { why("the head's pose did not come out as numbers"); held.state.live = false; return; }
    if (!SafeWrite((void*)matrices, pose, (size_t)held.count * sizeof(Mat))) { why("the head's pose could not be written"); return; }
    why(done == headhang::Done::Started ? "posed - hanging, started at rest under the hand" : "posed - hanging and swinging");
    if (!held.toldHung) {
        held.toldHung = true;
        static const char* const kBones[kHeadBones] = {"L_weapon_attach", "shield_attach", "R_weapon_attach"};
        char b[220];
        snprintf(b, sizeof b, "head hang: the hero's %s hangs on his %s - from its model's origin, the cord %.0f cm to the hanging part's middle", held.model.c_str(),
                 kBones[held.bone], held.state.length * 100.0f);
        NoteHead(b);
    }
}
#endif

#if D2RVR_FIRST_PERSON
// The way the palm faces, square to the forearm `axis` (unit, elbow -> wrist), model space: from the
// knuckles' line (pinky -> index), which a bending wrist turns about and so leaves alone - only the
// forearm's own twist turns it. As Fist works the palm out, the forearm in place of wrist -> middle
// knuckle. (The hand's axis most square to the forearm, picked afresh each frame, swapped for another
// as the wrist bent: the toolbar jumped onto the arm's edge.) The forearms' read (g_forearm), twice a
// pose with [hud] hand_vitals_orb_drawn_hand.
V3 PalmAcross(const Hero& h, int side, const std::vector<Mat>& newW, V3 axis) {
    const int k = h.hand[side];
    V3 across{0, 0, 0};
    if (const int i = h.ind[side], p = h.pnk[side]; i >= 0 && p >= 0) {
        V3 line = Sub(Pos(newW[i]), Pos(newW[p]));
        const float d = Dot(line, axis);
        line = V3{line.x - axis.x * d, line.y - axis.y * d, line.z - axis.z * d};
        across = Norm(Scale(Cross(axis, line), side == 0 ? -1.0f : 1.0f));
    } else {
        float best = 2.0f;
        for (int r = 0; r < 3; ++r) {   // no knuckles: the hand's axis most square to the forearm
            const V3 a = Norm(V3{newW[k].m[r * 4], newW[k].m[r * 4 + 1], newW[k].m[r * 4 + 2]});
            if (std::fabs(Dot(a, axis)) < best) { best = std::fabs(Dot(a, axis)); across = a; }
        }
        const float d = Dot(across, axis);
        across = Norm(V3{across.x - axis.x * d, across.y - axis.y * d, across.z - axis.z * d});
    }
    return across;
}
#endif

#if D2RVR_FIRST_PERSON
void WalkBackLegs(uintptr_t self, const Hero& h, const Input& in, float c, float s, std::vector<Mat>& newW);   // [body] walk_back (below)
std::atomic<bool> g_walkBackDumpOn{false};   // [debug] walk_back_dump (below)
#endif
void PoseBody(uintptr_t self, const Hero& h, const Input& in, const weight::Out& wo) {
    uintptr_t world = 0, mats = 0; int32_t n = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &n) || !Get(world + 4, &mats) || n != h.count) return;
    std::vector<Mat> oldW(n);
    if (!SafeCopy(oldW.data(), (const void*)mats, sizeof(Mat) * n)) return;
    std::vector<Mat> newW = oldW;
#if D2RVR_FIRST_PERSON
    g_fullReachPose = in.fullReach && in.mode == 2;   // [arms] full_reach: every Reach of this pose
    if (g_fullReachPose) NoteFullReach();
#endif
    {   // diagnostics: does the animation itself turn the hero?
        const int ids[3] = {0, h.spine >= 0 ? h.parent[h.spine] - 1 : 2, h.spine >= 0 ? h.parent[h.spine] : 3};
        for (int i = 0; i < 3; ++i)
            if (ids[i] >= 0 && ids[i] < n) g_poseYaw[i].store(atan2f(oldW[ids[i]].m[8], oldW[ids[i]].m[10]) * 57.2957795f);
    }
    const float c = cosf(in.yawInModel), s = sinf(in.yawInModel);

    // The hips above where they stand: a jump. The stride bobs them a little,
    // so up to jumpFrom nothing follows, and from there to twice that it eases
    // in to the whole lift - no step. Crouches and lunges (lower) are not lifts.
    float lift = 0.0f;
    if (h.eyeY > 0.0f) {
        const float raw = h.pelvis >= 0 && h.pelvisY > 0.0f ? oldW[h.pelvis].m[13] - h.pelvisY : 0.0f;
        const float a = std::max(in.jumpFrom, 0.01f);
        const float t = std::clamp((raw - a) / a, 0.0f, 1.0f);
        lift = in.followJump ? raw * t * t * (3.0f - 2.0f * t) : 0.0f;
        g_eyeY.store(h.eyeY);
        g_lift.store(lift);
        g_rawLift.store(raw);
        g_eyeAt.store(GetTickCount64());
    }

#if D2RVR_FIRST_PERSON
    // Nothing above the pelvis from the game: spine, neck, head, shoulders and
    // arms are re-hung in the bind pose (each bone's bind offset from its
    // parent, spine_01 at its bind place), so walking and attacking no longer
    // twist the trunk or shake the arms. Fingers and what the hands hold keep
    // the animation's grip. Pelvis and legs stay the game's.
    // [body] flat_hold_trunk (flat F3): the same, for the trunk's axis only - spine_01 up through the neck to the head in the bind pose, every
    // other bone above the pelvis (collarbones, arms, hands, the face) hung on it with the animation's own turn from its parent, so the arms
    // still swing as the game has them while the chest and the head stay under the camera; the hips held under it as below.
    const std::vector<char> axis = !in.lockUpper && in.holdTrunk && h.bindOk ? trunkhold::Axis(n, h.parent.data(), h.head, h.spine) : std::vector<char>{};
    const bool holdTrunk = !axis.empty();
    if ((in.lockUpper || holdTrunk) && h.bindOk) {
        for (int i = 0; i < n; ++i) {
            const int p = h.parent[i];
            switch (trunkhold::For(h.upper[i] != 0, i == h.spine || p < 0, h.inHand[i] != 0, in.lockUpper, holdTrunk, holdTrunk && axis[i])) {
                case trunkhold::Pose::Root: newW[i] = h.bind[i]; newW[i].m[13] += lift; break;   // up with the hips in a jump
                case trunkhold::Pose::Anim: newW[i] = Mul(Mul(oldW[i], InvAffine(oldW[p])), newW[p]); break;
                case trunkhold::Pose::Bind: newW[i] = Mul(Mul(h.bind[i], InvAffine(h.bind[p])), newW[p]); break;
                case trunkhold::Pose::Game: break;
            }
        }
        // [body] legs_under_body: the trunk now stands in its bind place, but
        // the hips are still the game's - and a blow lunges them ahead and
        // twists them (the barbarian's legs jumped out in front of him on
        // every swing). Everything below the trunk is moved back so the pelvis
        // stands where the bind pose has it across the floor, and turned so it
        // faces the bind pose's way. Its height stays the animation's (a
        // crouch, a jump), and the legs still step under it.
        if ((in.legsUnder || holdTrunk) && h.pelvis >= 0) {
            const Mat& a = oldW[h.pelvis];
            const Mat& b = h.bind[h.pelvis];
            // The turn taking the bind pelvis to the animation's, in model
            // space (W_anim = W_bind * M), and how far it swings model +z.
            const Mat M = Mul(InvAffine(Mat{b.m[0], b.m[1], b.m[2], 0, b.m[4], b.m[5], b.m[6], 0, b.m[8], b.m[9], b.m[10], 0, 0, 0, 0, 1}),
                              Mat{a.m[0], a.m[1], a.m[2], 0, a.m[4], a.m[5], a.m[6], 0, a.m[8], a.m[9], a.m[10], 0, 0, 0, 0, 1});
            const float yaw = atan2f(M.m[8], M.m[10]);
            const float cy = cosf(-yaw), sy = sinf(-yaw);
            Mat T{}; T.m[0] = cy; T.m[2] = -sy; T.m[5] = 1; T.m[8] = sy; T.m[10] = cy; T.m[15] = 1;
            // About the pelvis's own vertical, then over to the bind spot: p*T + t = (bind x, p.y, bind z).
            const V3 p = Pos(a);
            const V3 pt = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
            T.m[12] = b.m[12] - pt.x; T.m[14] = b.m[14] - pt.z;
            for (int i = 0; i < n; ++i) if (!h.upper[i]) newW[i] = Mul(oldW[i], T);
        }
    }

#endif
    // The body faces where the camera looks: everything turns about the
    // model's vertical (whole body, so the camera keeps its place by the head),
    // or only spine_01 and up about the spine's own vertical, legs left as walked.
    const bool turn = in.bodyTurn > 0 && in.haveYaw && in.yawInModel != 0.0f && (in.bodyTurn == 2 || h.spine >= 0);
    if (turn) {
        Mat T{}; T.m[0] = c; T.m[2] = -s; T.m[5] = 1; T.m[8] = s; T.m[10] = c; T.m[15] = 1;
        if (in.bodyTurn == 1) {   // pivot p: p - p*R
            const V3 p = Pos(oldW[h.spine]);
            T.m[12] = p.x - (p.x * c + p.z * s); T.m[14] = p.z - (-p.x * s + p.z * c);
        }
        for (int i = 0; i < n; ++i) if (in.bodyTurn == 2 || h.upper[i]) newW[i] = Mul(newW[i], T);
    }
#if D2RVR_FIRST_PERSON
    // [body] walk_back: turned whole to the look while the game walks him the other way - its walk mirrored, the legs step back (before the crouch: it bends them again)
    if (in.walkBack && turn && in.bodyTurn == 2 && !in.gaitLegs && h.bindOk && h.leg[0][2] >= 0 && h.leg[1][2] >= 0) WalkBackLegs(self, h, in, c, s, newW);
    // [body] crouch (vr/crouch.h; the idea is bearstonem/d2r-pcvr's, MIT, vr/skeletons.cpp PoseBody, its tracked-neck body): you
    // crouched and the camera went down with your head, so the whole body goes down by as much (in.crouchDrop, model units, vrcam
    // worked it out at the camera's own scale) and each leg is bent again - thigh and shin at their own lengths - to put its foot
    // back where the game's animation has it, the knee ahead. No deeper than the legs fold. Before the arms: the shoulders come
    // down with the body and the hands still go to the controllers. Drawn bones only: the hero's place, hit box, walking and
    // every game state are the game's. 0 (off, another view, standing): nothing here runs.
    if (in.crouchDrop > 0.0f) {
        const bool legs = h.bindOk && h.leg[0][2] >= 0 && h.leg[1][2] >= 0;
        g_crouchLegs.store(legs ? 1 : 0);
        float drop = legs ? in.crouchDrop : 0.0f;
        float thigh[2] = {}, shin[2] = {};
        for (int side = 0; side < 2 && drop > 0.0f; ++side) {
            const V3 a = Pos(newW[h.leg[side][0]]), b = Pos(newW[h.leg[side][1]]), z = Pos(newW[h.leg[side][2]]);
            thigh[side] = Len(Sub(b, a));
            shin[side] = Len(Sub(z, b));
            drop = std::min(drop, crouch::LegRoom({a.x, a.y, a.z}, {z.x, z.y, z.z}, thigh[side], shin[side]));
        }
        g_crouchApplied.store(drop);
        if (drop > 0.0f) {
            const std::vector<Mat> before = newW;
            for (int i = 0; i < n; ++i) newW[i].m[13] -= drop;
            // the way the legs face: turned with the whole body, else the model's own forward (legs_under_body holds them to it)
            const crouch::V ahead = turn && in.bodyTurn == 2 ? crouch::V{s, 0.0f, c} : crouch::V{0.0f, 0.0f, 1.0f};
            for (int side = 0; side < 2; ++side) {
                const int hip = h.leg[side][0], knee = h.leg[side][1], foot = h.leg[side][2];
                const V3 a = Pos(before[hip]), b = Pos(before[knee]), z = Pos(before[foot]), now = Pos(newW[hip]);
                crouch::V end{z.x, z.y, z.z}, bend;
                const crouch::V pole = crouch::KneePole({a.x, a.y, a.z}, {b.x, b.y, b.z}, end, ahead);
                if (!crouch::Knee({now.x, now.y, now.z}, thigh[side], shin[side], end, pole, bend)) continue;
                const std::vector<Mat> base = newW;
                AimChain(h, hip, knee, foot, {bend.x, bend.y, bend.z}, {end.x, end.y, end.z}, base, newW);
                // the foot keeps the animation's turn on the floor (a crouch bends the knee, not the ankle), its toes with it
                const V3 at = Pos(newW[foot]);
                newW[foot] = before[foot];
                newW[foot].m[12] = at.x; newW[foot].m[13] = at.y; newW[foot].m[14] = at.z;
                std::vector<char> moved(n, 0);
                moved[foot] = 1;
                Propagate(h, base, newW, moved, foot);
            }
        }
    } else {
        g_crouchApplied.store(0.0f);
    }
    // [openxr] roomscale_body (vr/room_walk.h): room-scale walking walks the hero through the game's own stick, and the game never walks
    // him slower than a quarter of his full walk - under that he catches up in short pulses while the view stays with your head. The
    // drawn body is moved across the floor to stand under your neck (in.roomShift: that point less the hero's place, worked out at
    // this pose from his model matrix as it is now, so his pulses do not show), before the arms: the shoulders come with it and the
    // hands still go to the controllers. Drawn bones only: the hero's place, hit box, walking and every game state are the game's.
    // [body] under_camera (vr/under_camera.h) rides on the same move: the body moved to where the camera stands on - back under it while the
    // game's camera trails him across the floor here, and up or down by roomShift[2] (a Leap's arc lifts his model off the camera's ground)
    // only at the end of the pose (t_poseUp): the arms and everything else are worked out with his feet at the model's own height.
    t_poseUp = 0.0f;
    if (in.roomShiftOn && std::isfinite(in.roomShift[0]) && std::isfinite(in.roomShift[1]) && std::isfinite(in.roomShift[2])) {
        for (int i = 0; i < n; ++i) { newW[i].m[12] += in.roomShift[0]; newW[i].m[14] += in.roomShift[1]; }
        t_poseUp = in.roomShift[2];
    }
    // A measured arm span (T-pose, [calibration] use=1; vr/body_calibration.h): each arm's bones, upper arm and forearm, are
    // scaled about their shoulder until the wrist stands where the controller's grip puts it at the world's scale - the hands
    // then reach as far as the real ones and the controllers stay 1:1. The hand, its fingers and what it holds are put back
    // at their authored size on the new wrist. Presentation only: no bone length the game reads, only the drawn ones.
    // The idea is bearstonem/d2r-pcvr's (MIT, vr/skeletons.cpp PoseBody).
    // [calibration] default_span: nothing measured - the span a person of the eye height has (bodycal::DefaultSpan), fitted the same way.
    const float armSpanM = in.armSpanM > 0.0f ? in.armSpanM : in.defaultSpan ? bodycal::DefaultSpan(in.userHeadM) : 0.0f;
    if (in.mode == 2 && armSpanM > 0.0f && h.bindOk && h.upr[0] >= 0 && h.upr[1] >= 0) {
        // the same eye height the hands are placed by (ControllerPoint)
        const float modelEye = in.haveEye ? in.eyeModel[1] : h.head >= 0 ? Pos(newW[h.head]).y : h.eyeY;
        const float shoulderWidth = Len(Sub(Pos(h.bind[h.upr[0]]), Pos(h.bind[h.upr[1]])));
        for (int side = 0; side < 2; ++side) {
            const int u = h.upr[side], l = h.lwr[side], k = h.hand[side];
            if (u < 0 || l < 0 || k < 0) continue;
            const float length = Len(Sub(Pos(h.bind[l]), Pos(h.bind[u]))) + Len(Sub(Pos(h.bind[k]), Pos(h.bind[l])));
            const float palm = h.mid[side] >= 0 ? 0.5f * Len(Sub(Pos(h.bind[h.mid[side]]), Pos(h.bind[k]))) : 0.0f;   // the wrist is that far behind the grip
            const float fit = bodycal::ArmScale(modelEye, in.userHeadM, armSpanM, shoulderWidth, length, palm);
            if (fit == 1.0f) continue;
            ScaleArmAbout(h, side, Pos(newW[u]), fit, newW);
        }
    }
    // [bow] aim_distance_by_arm: the arms as they stand now (shoulders after the turn, bones after the fit), for ArmReach - read, nothing is changed
    if (in.mode == 2) {
        const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : (h.head >= 0 ? Pos(newW[h.head]) : V3{0, 0, 0});
        const float upmArm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre, as ControllerPoint has it
        for (int side = 0; side < 2; ++side) {
            const int u = h.upr[side], l = h.lwr[side], k = h.hand[side];
            if (u < 0 || l < 0 || k < 0 || !(head.y > 0.0f)) continue;
            const V3 S = Pos(newW[u]);
            const float chain = Len(Sub(Pos(newW[l]), S)) + Len(Sub(Pos(newW[k]), Pos(newW[l])));
            const float palm = h.bindOk && h.mid[side] >= 0 ? 0.5f * Len(Sub(Pos(h.bind[h.mid[side]]), Pos(h.bind[k]))) : 0.0f;   // the grip is that far past the wrist
            const V3 d = Sub(S, head);
            const float mx = d.x * c - d.z * s, mz = d.x * s + d.z * c;   // ToModel run backwards: the turn out, then x and z flipped
            ArmReachOut o;
            o.shoulder[0] = -mx / upmArm; o.shoulder[1] = d.y / upmArm; o.shoulder[2] = -mz / upmArm;
            o.length = (chain + palm) / upmArm;
            o.at = GetTickCount64();
            if (!std::isfinite(o.shoulder[0] + o.shoulder[1] + o.shoulder[2] + o.length) || !(o.length > 0.05f)) continue;
            AcquireSRWLockExclusive(&g_armReachLock);
            g_armReach[side] = o;
            ReleaseSRWLockExclusive(&g_armReachLock);
        }
        // [gameplay] leap_arc_head: the belly - half way between the pelvis bone and spine_01 (the spine alone when the pelvis is the rig's root) - into the same frame
        if (h.spine >= 0 && head.y > 0.0f) {
            const V3 sp = Pos(newW[h.spine]);
            const V3 b = h.pelvis > 0 ? V3{0.5f * (sp.x + Pos(newW[h.pelvis]).x), 0.5f * (sp.y + Pos(newW[h.pelvis]).y), 0.5f * (sp.z + Pos(newW[h.pelvis]).z)} : sp;
            const V3 d = Sub(b, head);
            const float mx = d.x * c - d.z * s, mz = d.x * s + d.z * c;   // (as the shoulder above)
            BellyOut o;
            o.at3[0] = -mx / upmArm; o.at3[1] = d.y / upmArm; o.at3[2] = -mz / upmArm;
            o.at = GetTickCount64();
            if (std::isfinite(o.at3[0] + o.at3[1] + o.at3[2])) {
                AcquireSRWLockExclusive(&g_armReachLock);
                g_belly = o;
                ReleaseSRWLockExclusive(&g_armReachLock);
            }
        }
        // for the log, once per rig and span: the arms as drawn at the world's scale - where the hero's shoulders stand against your eyes and
        // how long each arm is to the grip (a hand farther than that from its shoulder is out of reach: [arms] full_reach)
        static uint64_t toldKey = 0;
        const uint64_t key = ((uint64_t)h.skel << 20) ^ (uint64_t)std::lround(armSpanM * 1000.0f) ^ ((uint64_t)std::lround(in.userHeadM * 1000.0f) << 10) ^ (in.fullReach ? 1ull << 63 : 0);
        if (key != toldKey && head.y > 0.0f) {
            ArmReachOut a[2];
            AcquireSRWLockShared(&g_armReachLock); a[0] = g_armReach[0]; a[1] = g_armReach[1]; ReleaseSRWLockShared(&g_armReachLock);
            if (a[0].at && a[1].at) {
                toldKey = key;
                char b[420];
                snprintf(b, sizeof b, "arms as drawn: the hero's shoulder joints %.2f m apart, the right one %.2f m out, %.2f m under and %.2f m %s the eyes; "
                         "shoulder to grip %.2f m right, %.2f m left (%s, eye height %.2f m)%s",
                         std::fabs(a[0].shoulder[0] - a[1].shoulder[0]), a[0].shoulder[0], -a[0].shoulder[1], std::fabs(a[0].shoulder[2]),
                         a[0].shoulder[2] >= 0.0f ? "behind" : "ahead of", a[0].length, a[1].length,
                         armSpanM > 0.0f ? ("fitted to the span " + std::to_string(armSpanM).substr(0, 5) + (in.armSpanM > 0.0f ? " m measured" : " m for the eye height ([calibration] default_span)")).c_str()
                                         : "the hero's own, not fitted",
                         in.userHeadM, in.fullReach ? "; [arms] full_reach: a hand farther than that brings its shoulder along, then lengthens the arm" : "");
                AcquireSRWLockExclusive(&g_armNoteLock);
                if (g_armNote.size() < 3000) g_armNote += (g_armNote.empty() ? "" : "\n") + std::string(b);
                ReleaseSRWLockExclusive(&g_armNoteLock);
            }
        }
    }
    // The animation after the turn: what the arms re-derive their children from.
    const std::vector<Mat> baseW = newW;

    float twistRoll[2] = {0.0f, 0.0f};   // [hands] forearm_twist: each hand's roll about its forearm, as TwistForearm read it
    bool twistDid[2] = {false, false};
    // Model space: y up, faces +z, its left is +x. The two arms' subtrees are
    // disjoint, and each pass re-derives only its own side.
    for (int side = 0; side < 2; ++side) {
        const int u = h.upr[side], l = h.lwr[side], k = h.hand[side];
        if (in.mode == 1) {
            const V3 S = Pos(newW[u]);
            const float L1 = Len(Sub(Pos(newW[l]), S)), L2 = Len(Sub(Pos(newW[k]), Pos(newW[l])));
            V3 fwd = {0, 0, 1};
            if (turn) fwd = {s, 0, c};
            AimChain(h, u, l, k, Add(S, Scale(fwd, L1)), Add(S, Scale(fwd, L1 + L2)), baseW, newW);
        } else if (in.mode == 2 && (in.handsValid & (1u << side)) && h.head >= 0) {
            V3 T = ControllerPoint(h, side, in, newW, c, s);
            const Wrist wr = PlanWrist(h, side, baseW, in, c, s);
            // The grip point is the middle of the palm; the wrist bone is half a hand behind it.
            if (wr.ok) T = Sub(T, Scale(wr.point, 0.5f * wr.reach));
            Reach(h, side, T, turn, c, s, baseW, newW);
            if (wr.ok) {
                ApplyWrist(newW[k], wr);
                std::vector<char> fromK(n, 0); fromK[k] = 1;
                Propagate(h, baseW, newW, fromK, k);
                if (in.forearmTwist >= 0.0f && h.bindOk) { twistRoll[side] = TwistForearm(h, side, in.forearmTwist, newW); twistDid[side] = true; }   // [hands] forearm_twist
            }
        } else if (in.lockUpper && h.bindOk) {
            // No controller for it and no animation either: the arm hangs
            // relaxed - down along the side, a little out, elbow a little bent.
            const V3 S = Pos(newW[u]);
            const float L1 = Len(Sub(Pos(newW[l]), S)), L2 = Len(Sub(Pos(newW[k]), Pos(newW[l])));
            V3 out = {side == 0 ? -1.0f : 1.0f, 0, 0}, fwd = {0, 0, 1};
            if (turn) { out = {out.x * c, 0, -out.x * s}; fwd = {s, 0, c}; }
            const V3 E = Add(S, Scale(Norm(Add({0, -1, 0}, Scale(out, 0.12f))), L1));
            const V3 T = Add(E, Scale(Norm(Add(Add({0, -1, 0}, Scale(fwd, 0.35f)), Scale(out, 0.05f))), L2));
            AimChain(h, u, l, k, E, T, baseW, newW);
        }
    }

    for (int side = 0; side < 2; ++side)
        if (!twistDid[side]) { g_twistLast[side] = NAN; g_twistOut[side].store(NAN); }   // read afresh when it comes back

    // The grip, taken once per weapon while not attacking (see Grip).
    Grip grip;
    AcquireSRWLockShared(&g_gripLock); grip = g_grip; ReleaseSRWLockShared(&g_gripLock);
    // [weight]: what the weight model draws is carried by this grip, so it must not be a HIDDEN bone's. The game hides what is not in a hand by shrinking its attach bone to a point
    // (a weapon swap in progress, the item hung on the other wrist walking): a grip taken then is a shrunk one, and the weapon drawn from it came out shrunk - 	he staff flattened
    // into a plane - for as long as the grip was kept. So: not taken while both attach bones are hidden; and a grip kept with the weapon's own wrist hidden is taken again once the
    // game shows the weapon on that wrist. (Weight off: as it was.)
    bool gripWanted = grip.type != in.weaponType || !grip.ok;
    if (wo.on && in.mode == 2 && h.weapon[0] >= 0 && h.weapon[1] >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
        float sz[2];
        for (int side = 0; side < 2; ++side) {
            const Mat a = Mul(oldW[h.weapon[side]], InvAffine(oldW[h.hand[side]]));
            sz[side] = Len({a.m[0], a.m[1], a.m[2]});
        }
        const int ws = in.weaponSide & 1;
        const bool shown = std::max(sz[0], sz[1]) > 0.3f;   // the game draws the weapon on some wrist (a hidden bone is a point)
        if (grip.ok && grip.type == in.weaponType && grip.scale[ws] < 0.3f && sz[ws] > 0.3f) gripWanted = true;   // a hidden bone's grip, and the game shows it there now
        if (!shown) gripWanted = false;
    }
    if (gripWanted && !in.attacking && !in.along && h.weapon[0] >= 0 && h.weapon[1] >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
        for (int side = 0; side < 2; ++side) {
            grip.attach[side] = Mul(oldW[h.weapon[side]], InvAffine(oldW[h.hand[side]]));
            grip.scale[side] = Len({grip.attach[side].m[0], grip.attach[side].m[1], grip.attach[side].m[2]});
        }
        grip.hand = grip.scale[0] > 0.7f && grip.scale[1] < 0.3f ? 0 : grip.scale[1] > 0.7f && grip.scale[0] < 0.3f ? 1 : -1;
        const int sb = h.weapon[in.staffHandAuto && grip.hand >= 0 ? grip.hand : in.staffHand == 0 ? 0 : 1];
        V3 pw[2];
        for (int side = 0; side < 2; ++side) {
            grip.inStaff[side] = Mul(oldW[h.hand[side]], InvAffine(oldW[sb]));
            V3 p = Pos(oldW[h.hand[side]]);
            if (h.mid[side] >= 0) p = Scale(Add(p, Pos(oldW[h.mid[side]])), 0.5f);
            pw[side] = p;
            p = Through(InvAffine(oldW[sb]), p);
            grip.palm[side][0] = p.x; grip.palm[side][1] = p.y; grip.palm[side][2] = p.z;
        }
        grip.type = in.weaponType;
        grip.ok = true;
        if (wo.on) {   // told, so the log says what the weapon was drawn from
            char b[200];
            snprintf(b, sizeof b, "the game's grip taken for weapon type %d: its attach bones as the animation has them - right %.3f, left %.3f (the weapon in the %s slot)", in.weaponType, grip.scale[0], grip.scale[1], in.weaponSide ? "left" : "right");
            weight::Say(b);
        }
        // A crossbow is placed by where the game's right hand holds it (GunFrame): a grip
        // taken in a pose with the hands off it (4.48 units apart, not the 0.80 of the
        // hold) put the model a metre off (2026-10-05). Such a one is not taken; the last
        // good one stands in for it.
        // [hands] xbow_grip_wide: placed by the controller (gun frame, a fixed stock axis) the pose only gives the attach relation,
        // so a wide one (the idle after a swap from a shield set) is taken too, while the attach bone is shown (xbowgrip::WideCaptureAllowed).
        static Grip goodXbow;
        const bool controllerPlaced = in.xbowWide && in.gunFrame && in.stockAxis != 0 && abs(in.stockAxis) <= 3;
        const float mountScale = grip.scale[grip.hand >= 0 ? grip.hand : in.staffHand == 0 ? 0 : 1];
        if (in.gunStock && !xbowgrip::WideCaptureAllowed(Len(Sub(pw[0], pw[1])), controllerPlaced, mountScale)) {
            if (goodXbow.ok && goodXbow.type == in.weaponType) grip = goodXbow;
            else grip.ok = false;
        } else if (in.gunStock) {
            goodXbow = grip;
        }
        if (grip.ok) { AcquireSRWLockExclusive(&g_gripLock); g_grip = grip; ReleaseSRWLockExclusive(&g_gripLock); }
    }
    // [weight] grip_fit_fingers: the game's weapon-grip pose of the fingers, read off the hand the game holds the weapon in (the slot's) - from the game's own matrices, so whatever
    // the weight model does to that hand (it lets go, the other carries the weapon) does not touch it - while the hero is calm (a swing's hand is not a grip) and the game shows the
    // weapon on that wrist (a hidden attach bone: nothing is held there). Kept for the weapon type; refreshed each calm pose, so it is the base hand's pose as it is drawn.
    if (wo.on && in.mode == 2 && ((in.weight.gripFit && in.weight.gripFingers) || wo.gripLib) && !in.attacking && !in.along && h.bindOk) {
        const int bs = wo.slot & 1;
        if (h.weapon[bs] >= 0 && h.hand[bs] >= 0) {
            const Mat a = Mul(oldW[h.weapon[bs]], InvAffine(oldW[h.hand[bs]]));
            GripPoseKept kept;
            if (Len({a.m[0], a.m[1], a.m[2]}) > 0.7f && TakeGripPose(h, bs, oldW, in.weaponType, &kept)) {
                AcquireSRWLockExclusive(&g_gripPoseLock);
                g_gripPose = std::move(kept);
                ReleaseSRWLockExclusive(&g_gripPoseLock);
            }
        }
    }
    // staff_free_left: the staff goes with the right hand alone, whichever wrist
    // the game hangs it on (the left one, on the sorceress) - held there the way
    // the game's grip has it: the staff relative to the right wrist.
    const int staffSide = in.staffHandAuto && grip.ok && grip.hand >= 0 ? grip.hand : in.staffHand == 0 ? 0 : 1;
    // [hands] xbow_handover: which controller holds the crossbow (0 right, the home; 1 left) - the other hand is its support. The free hand
    // takes it by its grip with a new squeeze at the holding palm (xbowgrip::Handover); the game's slots and attack are not touched.
    const int gunHolder = XbowHolderStep(h, in, newW, c, s);
    const int holdSide = in.gunStock ? gunHolder : 0;
    const bool freeStaff = in.staff && in.staffFreeLeft && in.mode == 2 && in.holdGrip && grip.ok && (in.handsValid & (1u << holdSide)) &&
                           h.head >= 0 && h.weapon[staffSide] >= 0;
    // Held weapons sit in that grip, not in this frame's animation (a bow keeps the animation: it draws).
    if (in.mode == 2 && in.holdGrip && grip.ok) {
        for (int side = 0; side < 2; ++side) {
            const int wb = h.weapon[side], k = h.hand[side];
            if (wb < 0 || k < 0) continue;
            // The game hides what leaves the hand (a thrown weapon, a potion)
            // by shrinking its attach bone: that stays the game's.
            const Mat anim = Mul(oldW[wb], InvAffine(oldW[k]));
            g_attachSize[side].store(Len({anim.m[0], anim.m[1], anim.m[2]}));
            // bothAttach: the holding hand's bone is the weapon's whatever the game does with it -
            // it shrinks it away while it moves the weapon to the other wrist (a great axe while
            // walking): copied shrunk to the other one, the axe vanished (2026-10-06).
            const bool holder = in.bothAttach && side == (freeStaff ? staffSide : in.weaponSide ? 1 : 0);
            if (Len({anim.m[0], anim.m[1], anim.m[2]}) < 0.3f && !holder) newW[wb] = Mul(anim, newW[k]);
            else newW[wb] = freeStaff && side == staffSide ? Mul(InvAffine(grip.inStaff[0]), newW[h.hand[0]]) : Mul(grip.attach[side], newW[k]);
            std::vector<char> fromWeapon(n, 0);
            fromWeapon[wb] = 1;
            Propagate(h, oldW, newW, fromWeapon, wb);
        }
    }

    // A crossbow is held like a gun: its bone is set in the right controller's own
    // frame - the origin (where the game hangs it) in the middle of the right palm,
    // the stock (the bone axis nearest the line between the palms in the game's
    // grip) where the controller points, the next bone axis along its top.
    // [weapon_crossbow] then turns it about the controller's axes (pitch about its
    // right, yaw about its top, roll about where it points) and moves it along them,
    // cm. Taken from the game's grip, laid along the hand and turned in the bone's
    // own axes, the stock lay askew, the bolts flew along it - to the left - and the
    // crossbow hung from a point beside the hand (2026-10-05). The shot is the
    // controller's -Z (vrcam HandRay), whatever the model.
    V3 gunF{}, gunU{}, gunR{}, gunAt{}, gunStock{};
    const bool gun = freeStaff && in.gunStock && in.gunFrame && h.hand[holdSide] >= 0 && GunFrame(in, c, s, &gunF, &gunU, &gunR, holdSide);
    if (gun) {
        const int wb = h.weapon[staffSide];
        Mat staff = newW[wb];
        const V3 inR = {grip.palm[0][0], grip.palm[0][1], grip.palm[0][2]}, inL = {grip.palm[1][0], grip.palm[1][1], grip.palm[1][2]};
        const V3 v = Sub(inL, inR);   // staff frame, from the right palm toward the left one
        const float av[3] = {fabsf(v.x), fabsf(v.y), fabsf(v.z)};
        int ai = av[0] >= av[1] && av[0] >= av[2] ? 0 : av[1] >= av[2] ? 1 : 2;
        float sign = (ai == 0 ? v.x : ai == 1 ? v.y : v.z) < 0.0f ? -1.0f : 1.0f;
        // The grip is taken again from whatever pose the animation has (F11, a weapon
        // change): the line between its palms leaned toward another axis and the model
        // jumped a quarter turn (2026-10-05). [hands] xbow_stock_axis holds it.
        if (in.stockAxis >= -3 && in.stockAxis <= 3 && in.stockAxis != 0) {
            ai = abs(in.stockAxis) - 1;
            sign = in.stockAxis < 0 ? -1.0f : 1.0f;
        }
        g_gunAxis.store((ai + 1) * (int)sign);
        float len[3];
        V3 row[3];
        for (int i = 0; i < 3; ++i) row[i] = {staff.m[i * 4], staff.m[i * 4 + 1], staff.m[i * 4 + 2]};
        for (int i = 0; i < 3; ++i) len[i] = Len(row[i]);
        const float det = Dot(row[0], Cross(row[1], row[2])) < 0.0f ? -1.0f : 1.0f;   // the bone's handedness kept
        const int up = (ai + 1) % 3, third = (ai + 2) % 3;
        row[ai] = Scale(gunF, sign);
        row[up] = gunU;
        row[third] = Scale(Cross(row[(third + 1) % 3], row[(third + 2) % 3]), det);
        // [weapon_crossbow]'s turn, about the controller's axes; the model and the line
        // the bolt and the left hand keep to turn with it.
        const float* a = in.weaponAdj;
        const Mat adj = Mul(Mul(TurnAbout(gunF, a[5]), TurnAbout(gunR, a[3])), TurnAbout(gunU, a[4]));
        const Mat T = adj;
        for (int i = 0; i < 3; ++i) {
            const V3 r = Scale(Norm(RotOnly(row[i], T)), len[i]);
            staff.m[i * 4] = r.x; staff.m[i * 4 + 1] = r.y; staff.m[i * 4 + 2] = r.z;
        }
        // [hands] xbow_barrel_forward: laid by its OWN barrel - the drawn barrel (its bolt_helper, the renderer's placing, kept per weapon) along
        // where the controller points, the crossbow's up along the controller's top - instead of a bone axis set by hand (xbow_stock_axis), which
        // left the barrel ~34 deg above the controller's forward and 4-10 deg aside (0.170: it pointed up-left and twisted in the hand; the shot
        // needed a -46 deg line). Until the barrel is read: as before. [weapon_crossbow]'s turn is not added on top then.
        {
            const uint64_t bk = ((uint64_t)HeroGen() << 32) | (uint32_t)in.weaponType;
            bool laid = false;
            if (in.xbowBarrelForward && g_xbowHeldKey == bk && g_xbowHeldBarrel.at) {
                const xbowgrip::M& ia = g_xbowHeldBarrel.inAttach;
                xbowgrip::M sm, turned;
                memcpy(sm.m, staff.m, sizeof sm.m);
                if (xbowgrip::HandTurnTo(sm, xbowgrip::Row(ia, 2), xbowgrip::Row(ia, 1), {gunF.x, gunF.y, gunF.z}, {gunU.x, gunU.y, gunU.z}, &turned)) {
                    memcpy(staff.m, turned.m, sizeof staff.m);
                    laid = true;
                }
            }
            static int toldLaid = -1;
            if (in.xbowBarrelForward && (laid ? 1 : 0) != toldLaid) {
                toldLaid = laid ? 1 : 0;
                XbowNote(laid ? "crossbow laid by its own barrel: the drawn barrel along the controller's forward, its up along the controller's top ([hands] xbow_barrel_forward)"
                              : "crossbow laid by the bone axis as before - its drawn barrel is not read yet ([hands] xbow_barrel_forward)");
            }
        }
        // [hands] xbow_line_*: the line alone turned, set by eye onto the model as drawn
        const float* lt = in.lineTurnDeg;
        const Mat line = Mul(Mul(TurnAbout(gunF, lt[2]), TurnAbout(gunR, lt[0])), TurnAbout(gunU, lt[1]));
        gunStock = Norm(RotOnly(RotOnly(gunF, line), adj));
        const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
        const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
        // Fast to the controller, nothing from the animation: placed by where the game's
        // own right hand held it, it moved with the pose the grip was taken in (its palms
        // 0.80 apart just after taking it, 1.3 standing, 4.5 in a stride - 2026-10-05).
        // The bone's origin in the palm, moved along the controller's axes by
        // [weapon_crossbow] x/y/z until the stock runs through the palm; the line the
        // bolt and the left hand keep to runs from the palm along the stock.
        gunAt = ControllerPoint(h, holdSide, in, newW, c, s);
        // [hands] xbow_hold_grip: the crossbow's GRIP in the right palm - where the game's own hand holds it (its palm's middle in the
        // attach bone's frame, grip.palm of the wrist the game hangs it on: a rigid child of that hand, so the same in any pose) - not the
        // attach bone's origin (test-58..66: "it holds the crossbow by the wrist, not the hand"). Off: the origin in the palm, as before.
        V3 held = gunAt;
        {
            // the game's TRIGGER hand's palm on the crossbow (its other hand than the one it hangs it on), learned from the game's own two-handed
            // hold (g_xbowFore, below); 0.169 put the fore-grip hand's palm here - the right hand held it mid-stock under the prod
            const int gs = (staffSide & 1) ^ 1;
            const bool learned = g_xbowFore.n >= kForeGripSamples && g_xbowFore.key == (((uint64_t)HeroGen() << 32) | (uint32_t)in.weaponType);
            const V3 g = g_xbowFore.trigPalm;
            const float gripCm = Len(RotOnly(g, staff)) / upm * 100.0f;   // how far the game's trigger palm is from the attach bone's origin
            const bool use = in.xbowHoldGrip && learned && std::isfinite(gripCm) && gripCm > 0.3f && gripCm < 40.0f;
            if (use) {
                xbowgrip::M sm;
                memcpy(sm.m, staff.m, sizeof sm.m);
                const xbowgrip::V o = xbowgrip::OriginForGrip(sm, {g.x, g.y, g.z}, {gunAt.x, gunAt.y, gunAt.z});
                held = {o.x, o.y, o.z};
            }
            static int toldGrip = -1;   // which point is used, once per weapon and choice
            const int key = (in.weaponType & 0x0FFFFFFF) * 2 + (use ? 1 : 0);
            if (key != toldGrip && grip.ok && (use || !in.xbowHoldGrip || learned)) {
                toldGrip = key;
                char t[300];
                snprintf(t, sizeof t, "crossbow holding hand: %s - the game's trigger palm sits %.1f cm from the crossbow's attach bone (its %s hand's)%s",
                         use ? "the crossbow's trigger grip (the game's own trigger hand's palm on it) in the holding palm ([hands] xbow_hold_grip)"
                             : "the crossbow's attach bone in the holding palm (as before)",
                         gripCm, gs ? "left" : "right",
                         in.xbowHoldGrip && !use ? " - not used: out of 0.3 .. 40 cm" : "");
                XbowNote(t);
            }
        }
        const V3 at = Add(held, Add(Scale(gunR, a[0] * 0.01f * upm), Add(Scale(gunU, a[1] * 0.01f * upm), Scale(gunF, a[2] * 0.01f * upm))));
        staff.m[12] = at.x; staff.m[13] = at.y; staff.m[14] = at.z;
        newW[wb] = staff;
        std::vector<char> fromStaff(n, 0);
        fromStaff[wb] = 1;
        Propagate(h, oldW, newW, fromStaff, wb);
    }

    // [weapon_<kind>]: what the game hangs on a wrist (R/L_weapon_attach) sits
    // where its own animation put the hand, not where the controller does - a
    // bow came out a little turned in the fist. Moved and turned in the attach
    // bone's own axes, per kind of weapon, set by eye on the Weapon Adjust tab.
    if (in.mode == 2) {
        const float* a = in.weaponAdj;
        if (a[0] != 0.0f || a[1] != 0.0f || a[2] != 0.0f || a[3] != 0.0f || a[4] != 0.0f || a[5] != 0.0f) {
            const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head >= 0 ? h.head : 0]).y;
            const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
            auto turn = [](int axis, float deg) {   // row-vector rotation about one of the bone's own axes
                Mat r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
                const float t = deg * 0.0174532925f, c = cosf(t), s = sinf(t);
                const int i = (axis + 1) % 3, j = (axis + 2) % 3;
                r.m[i * 4 + i] = c; r.m[i * 4 + j] = s; r.m[j * 4 + i] = -s; r.m[j * 4 + j] = c;
                return r;
            };
            const Mat local = Mul(Mul(turn(0, a[3]), turn(1, a[4])), turn(2, a[5]));
            for (int side = 0; side < 2; ++side) {
                const int wb = h.weapon[side], holder = freeStaff && side == staffSide ? 0 : side;
                if (wb < 0 || !(in.handsValid & (1u << holder))) continue;
                if (gun && side == staffSide) continue;   // the crossbow's [weapon_crossbow] is the controller's (above)
                if (side == 1 && in.shieldForearm) continue;   // [shield] forearm: the shield has its own (StrapShield)
                V3 shift{0, 0, 0};
                for (int r = 0; r < 3; ++r) {
                    const V3 axis = Norm(V3{newW[wb].m[r * 4], newW[wb].m[r * 4 + 1], newW[wb].m[r * 4 + 2]});
                    shift = Add(shift, Scale(axis, a[r] * 0.01f * upm));
                }
                newW[wb] = Mul(local, newW[wb]);   // turned in its own frame, about its own origin
                newW[wb].m[12] += shift.x; newW[wb].m[13] += shift.y; newW[wb].m[14] += shift.z;
                std::vector<char> fromWeapon(n, 0);
                fromWeapon[wb] = 1;
                Propagate(h, oldW, newW, fromWeapon, wb);
            }
        }
    }

    // [bow] aim_along_hands: the bow, its arrow and the bow hand lie along the line from the string hand to the bow hand while the
    // string is taken (and the bow's real axis is read for the debug): the bow hand is turned and its arm solved again, above.
    // [bow] string_anchor: where the string is held (the string hand's wrist is final here: the arms are placed above), for the aim and for the
    // bow's own rig below (NoteBowHands).
    g_fingersCarried = false;
    if (in.mode == 2 && in.rangedHolster && in.rangedBow && in.rangedSwap && !in.rangedStowed && (in.rangedSide == 0 || in.rangedSide == 1)) {
        // [hands] shoulder_draw_ranged_swap: the bow was drawn into the other hand: its attach bone is carried there before anything is posed round it
        const int boneSide = BowBoneSide(h, in, oldW, in.rangedSide);
        float sideAxis[3], f[3], u[3];
        const bool axes = BowAxesNow(f, u, sideAxis);
        if (BowCarry(h, oldW, newW, boneSide, in.rangedSide, axes ? sideAxis : nullptr)) g_fingersCarried = CarryBowFingers(h, oldW, newW, boneSide, in.rangedSide);
    }
    StringAnchor anchor;
    if (in.bowStringAnchor && in.mode == 2 && (in.bowAim || in.bowAxis || in.bowRest || in.bowRig || in.bowString)) anchor = StringAnchorNow(h, in, oldW, newW, c, s);
    if ((in.bowAim || in.bowAxis || in.bowRest) && in.mode == 2 && in.holdGrip && grip.ok && !gun) AimBow(h, in, oldW, baseW, newW, c, s, turn, n, anchor);
    bool weightDrew = false;     // [weight]: the weight block below set the weapon bones (both attach bones are W): nothing after moves them
    LibGripResult libGripRes[2];  // [weight] grip_model 1: the grip library's answer for each hand in this pose (the hand laid in the weight block, the fingers after it)
    for (int sd = 0; sd < 2; ++sd)
        if (!wo.on || !wo.holds[sd]) LibGripForget(sd);   // a hand that holds nothing: its next grab starts afresh
    // [weight] (vr/weight.cpp): the weapon is carried by the hand weight::Apply says (its controller was
    // moved onto the weapon), in the relation that hand took it in: where the game's grip and Weapon
    // Adjust put it in the slot's fist, or where the hand closed on the shaft. A hand fully on it beside
    // the carrying one has its own relation taken each pose, so the weapon stays put when that one lets go.
    if (wo.on && in.mode == 2 && h.weapon[wo.slot] >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
        const int slot = wo.slot, wb = h.weapon[slot], m = wo.main & 1;
        Mat home = Mul(newW[wb], InvAffine(newW[h.hand[slot]]));   // as placed above
        // The scale a weapon bone should have: the game's own attach bone as it draws the weapon now (against its wrist), the widest of the two (the other is hidden - shrunk to a
        // point - when it holds nothing). A weapon matrix that came out far smaller (a grip taken with the bone hidden, a stale relation) is a flattened weapon: brought back.
        float gameLocal = 0.0f;
        for (int sd = 0; sd < 2; ++sd)
            if (h.weapon[sd] >= 0 && h.hand[sd] >= 0) {
                const Mat a = Mul(oldW[h.weapon[sd]], InvAffine(oldW[h.hand[sd]]));
                gameLocal = std::max(gameLocal, Len({a.m[0], a.m[1], a.m[2]}));
            }
        boneguard::Fault homeFault = boneguard::Fault::None;
        if (gameLocal > 0.3f) homeFault = boneguard::Restore(home.m, gameLocal, 1);   // (a bone against its own wrist: both mirrored or neither - right-handed)
        // the handedness the weapon bone has: its slot's attach bone's (a right one proper, a left one a mirror), whatever the scale it was shrunk to
        const double slotDet = boneguard::Det(oldW[wb].m);
        const int wSign = slotDet != 0.0 ? (slotDet < 0.0 ? -1 : 1) : (slot == 1 ? -1 : 1);
        AcquireSRWLockExclusive(&g_weightLock);
        WeightRel r = g_weightRel;
        if (wo.asPlaced) { r.rel[slot] = home; r.grabs[slot] = wo.grabs[slot]; r.ok[slot] = true; r.ok[slot ^ 1] = false; }
        // The weight model's body as a matrix in his model space: its frame (the fist that held it first)
        // turned and placed as a controller there is, the same handOffset beside it as the hands.
        const V3 headM = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
        const float upmM = std::clamp(headM.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
        const auto bodyMatrix = [&](const float body[3], const float rot[4]) {   // (a weight model's body frame in BodyWalk's frame -> his model space)
            Mat bm{};
            Q4 q = {rot[0], rot[1], rot[2], rot[3]};
            const float ql = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            q = ql > 0.5f ? Q4{q.x / ql, q.y / ql, q.z / ql, q.w / ql} : Q4{0, 0, 0, 1};
            const V3 axes[3] = {QRot(q, {1, 0, 0}), QRot(q, {0, 1, 0}), QRot(q, {0, 0, 1})};
            for (int r3 = 0; r3 < 3; ++r3) {
                const float v[3] = {axes[r3].x, axes[r3].y, axes[r3].z};
                const V3 a = ToModel(v, c, s);
                bm.m[r3 * 4] = a.x; bm.m[r3 * 4 + 1] = a.y; bm.m[r3 * 4 + 2] = a.z;
            }
            const float hv[3] = {body[0] + in.handOffset[0], body[1] + in.handOffset[1], body[2] + in.handOffset[2]};
            const V3 p = Add(headM, Scale(ToModel(hv, c, s), upmM));
            bm.m[12] = p.x; bm.m[13] = p.y; bm.m[14] = p.z; bm.m[15] = 1.0f;
            return bm;
        };
        // [weight] dual_collide with [gameplay] melee_head_volume: a weapon's head as its own volume (in.weight.headA/B/R, the model's units) where its drawn bone puts it,
        // against its body (metres, the frame its drawn shaft is measured in) - told to the weight model; none told as none
        static_assert(skel::Input::Weight::kHeadParts == weight::kHeadParts, "the skeleton's head parts are the weight model's");
        const auto drawnHead = [&](const Mat& bone, const Mat& body, int which, void (*tell)(const float*, int)) {
            if (!(weaponextent::UnitM(Len(V3{bone.m[4], bone.m[5], bone.m[6]}), upmM) > 0.0f)) return;   // (a bone shrunk or squeezed: no weapon's own size - the last head stands)
            float parts[weight::kHeadParts][7];
            int n = 0;
            for (int i = 0; i < in.weight.headParts[which] && i < weight::kHeadParts; ++i) {
                const float* p = in.weight.headPart[which][i];
                weaponshaft::Head hd;
                if (!(p[6] > 0.0f) || !weaponshaft::HeadIn(bone.m, body.m, upmM, p, p + 3, p[6], &hd)) { n = 0; break; }   // (all of it or none)
                for (int k = 0; k < 3; ++k) { parts[n][k] = (float)hd.a[k]; parts[n][3 + k] = (float)hd.b[k]; }
                parts[n][6] = (float)hd.radius;
                ++n;
            }
            tell(&parts[0][0], n);
        };
        const Mat bodyM = bodyMatrix(wo.body, wo.bodyRot);
        int second2B = -1;  // [weight] second_lets_go: the second weapon's attach bone as this pose sets it (below), and its matrix - set again after the hands are laid
        int hidden2B = -1;  // [hands] throw_left_hand: the second weapon's attach bone while the game throws it (Out::hidden2): squeezed to a point at the end, as the first's
        Mat second2W{};
        const bool have = r.ok[m] && r.grabs[m] == wo.grabs[m];
        // [weight] after_drop 1: a weapon let go that has landed and lain is slung on the hero's back, relative to his spine bone (vr/weapon_back.h):
        // it follows his body - turn, lean, crouch - and the body frame the weight model keeps for it is told that place (SetBackBody), so
        // the weapon flies from there when a hand takes it or calls it back. 2: the same place is kept (the flight starts from it) but the
        // weapon is not drawn. Only its drawing moves; the game's weapon stays the game's.
        Mat W{};
        bool onBack = false;
        if (wo.stowed && in.weight.afterDrop >= 1 && h.bindOk && h.spine >= 0 && r.bodyOk) {
            weaponback::Mat bind, now, cur, placed;
            memcpy(bind.m, h.bind[h.spine].m, sizeof bind.m);
            memcpy(now.m, newW[h.spine].m, sizeof now.m);
            const Mat curW = Mul(r.body, bodyM);   // as the body model has it: only its scale and handedness are taken
            memcpy(cur.m, curW.m, sizeof cur.m);
            const weaponback::Tuning t{in.weight.back[0], in.weight.back[1], in.weight.back[2], in.weight.back[3], in.weight.back[4], in.weight.back[5]};
            // (the body's size, not the arm reach on top of it: `scale` stretches the hands, not the back)
            // (a weapon in each hand: each behind its own hand's shoulder - the long way, the tip up over that shoulder; the left one mirrored)
            if (weaponback::BackMatrix(bind, now, cur, in.weight.tipAxis, wo.midM, upmM / std::max(0.1f, in.scale),
                                       in.weight.backLong || wo.second ? weaponback::Class::Long : weaponback::Class::Short, t, &placed, wo.second && slot == 1)) {
                memcpy(W.m, placed.m, sizeof W.m);
                onBack = true;
            }
        }
        // A hand that took it alone and is not the slot's (the left, off the floor or the back): until its own relation is taken it is
        // drawn where the body model has it - not jumped to the slot's hand. [weight] after_drop 1 or 2 only: 0, as it was.
        const bool bodyDrawn = !wo.free && !have && m != slot && in.weight.afterDrop >= 1 && r.bodyOk;
        // let go (falling, on the floor, flying back): where the body is; in a hand: on that hand
        if (!onBack)
            W = (wo.free || bodyDrawn) ? (r.bodyOk ? Mul(r.body, bodyM) : Mul(home, newW[h.hand[slot]]))
                                       : Mul(have ? r.rel[m] : home, newW[h.hand[have ? m : slot]]);
        // ... and the weapon as drawn, whichever way it was made: the scale of the game's attach bone times the carrying wrist's (W = attach against the wrist, carried by the wrist)
        boneguard::Fault wFault = homeFault;
        if (!wo.free && !onBack && gameLocal > 0.3f) {
            const int carrier = h.hand[have && !bodyDrawn ? m : slot];
            const double want = (double)gameLocal * boneguard::RowLen(newW[carrier].m, 0);
            const boneguard::Fault f = boneguard::Restore(W.m, want, wSign);
            if (f != boneguard::Fault::None) {
                wFault = f;
                if (!bodyDrawn) r.rel[have ? m : slot] = Mul(W, InvAffine(newW[carrier]));   // (the relation kept was the shrunk one)
            }
        }
        {
            static boneguard::Fault told = boneguard::Fault::None;   // told once for each stretch of a fault, and again when a later one comes
            if (wFault != told && wFault != boneguard::Fault::None) {
                char b[220];
                snprintf(b, sizeof b, "the weapon's matrix came out %s: set right (the game's attach bones: wanted scale %.2f)", boneguard::FaultName(wFault), gameLocal);
                weight::Say(b);
            }
            told = wFault;
        }
        if (!wo.free) { r.body = Mul(W, InvAffine(bodyM)); r.bodyOk = true; }
        if (bodyDrawn && wo.holds[m] && wo.on01[m] >= 1.0f) { r.rel[m] = Mul(W, InvAffine(newW[h.hand[m]])); r.grabs[m] = wo.grabs[m]; r.ok[m] = true; }
        const int o = m ^ 1;
        if (!wo.free && wo.holds[o] && wo.on01[o] >= 1.0f) { r.rel[o] = Mul(W, InvAffine(newW[h.hand[o]])); r.grabs[o] = wo.grabs[o]; r.ok[o] = true; }
        g_weightRel = r;
        // [weight] second_lets_go: a weapon in each hand - the second one's attach bone kept against its body while it is in its hand, and drawn where that body is
        // while it is let go (falling, lying, flying back). Only its drawing: the game's weapon stays the game's, in that hand.
        second2B = -1;
        if (wo.second && (wo.slot2 & 1) != slot && h.weapon[wo.slot2 & 1] >= 0) {
            const int b2 = h.weapon[wo.slot2 & 1];
            if (wo.hidden2) hidden2B = b2;
            const Mat body2M = bodyMatrix(wo.body2, wo.body2Rot);
            const auto measure2 = [&](const Mat& w2) {   // its shaft as drawn against its body: a hand that takes it closes on the drawn weapon
                if (in.weight.shaft2TipM + in.weight.shaft2ButtM <= 0.01f) return;
                if (!(weaponextent::UnitM(Len(V3{w2.m[4], w2.m[5], w2.m[6]}), upmM) > 0.0f)) return;   // (its bone shrunk - thrown, hidden by the game: the last good measurement stands)
                const weaponshaft::Line ln = weaponshaft::Relative(w2.m, body2M.m, upmM, in.weight.secondTipAxis, in.weight.shaft2TipM, in.weight.shaft2ButtM);
                if (!ln.ok) return;
                const float a[3] = {(float)ln.at[0], (float)ln.at[1], (float)ln.at[2]}, d[3] = {(float)ln.dir[0], (float)ln.dir[1], (float)ln.dir[2]};
                weight::SetDrawnShaft2(a, d, (float)ln.butt, (float)ln.tip, (float)ln.boneAlong);
                drawnHead(w2, body2M, 1, &weight::SetDrawnHeadParts2);   // [weight] dual_collide: its head's own volume, as drawn
            };
            if (wo.hand2 == slot && h.hand[slot] >= 0) {
                // the HAND-OVER: the first's hand holds the second - it is laid in that hand as the game's own grip has a weapon there (the slot's attach bone against
                // its hand, as placed: the first weapon's base hand), the left bone's handedness kept (a mirror-less matrix draws the item inside out); its body and
                // shaft follow it (the user, 2026-10-24: «оружие сбоку» - laid by the left hand's grip it hung beside the right hand)
                Mat W2 = Mul(home, newW[h.hand[slot]]);
                if ((boneguard::Det(oldW[b2].m) < 0.0) != (boneguard::Det(W2.m) < 0.0))
                    for (int c2 = 0; c2 < 3; ++c2) W2.m[c2] = -W2.m[c2];
                newW[b2] = W2;
                std::vector<char> fromWeapon(n, 0);
                fromWeapon[b2] = 1;
                Propagate(h, oldW, newW, fromWeapon, b2);
                second2B = b2;
                second2W = W2;
                g_weightRel2.body = Mul(W2, InvAffine(body2M));
                g_weightRel2.ok = true;
                if (!g_weightRel2.handOver) measure2(W2);   // (once as it is taken: its weight body on the weapon as laid; a measure each frame re-lays the hand, the hand moves it)
                g_weightRel2.handOver = true;
            } else if (!wo.free2) {
                g_weightRel2.handOver = false;
                g_weightRel2.body = Mul(newW[b2], InvAffine(body2M));
                g_weightRel2.ok = true;
                measure2(newW[b2]);   // (the first's is measured below)
            } else if (g_weightRel2.ok) {
                g_weightRel2.handOver = false;   // (let go: taken by the other hand again, it is measured again)
                Mat W2 = Mul(g_weightRel2.body, body2M);
                bool onBack2 = false;
                // [weight] after_drop 1 or 2: on the back, behind its own hand's shoulder (the left weapon behind the left one: the place mirrored)
                if (wo.stowed2 && in.weight.afterDrop >= 1 && h.bindOk && h.spine >= 0) {
                    weaponback::Mat bind, now, cur, placed;
                    memcpy(bind.m, h.bind[h.spine].m, sizeof bind.m);
                    memcpy(now.m, newW[h.spine].m, sizeof now.m);
                    memcpy(cur.m, W2.m, sizeof cur.m);
                    const weaponback::Tuning t{in.weight.back[0], in.weight.back[1], in.weight.back[2], in.weight.back[3], in.weight.back[4], in.weight.back[5]};
                    if (weaponback::BackMatrix(bind, now, cur, in.weight.tipAxis, wo.midM2, upmM / std::max(0.1f, in.scale), weaponback::Class::Long, t, &placed,
                                               (wo.slot2 & 1) == 1)) {
                        memcpy(W2.m, placed.m, sizeof W2.m);
                        onBack2 = true;
                    }
                }
                if (onBack2) {   // its place on the back, in BodyWalk's frame: the weapon flies from there when its hand draws it (W2 = rel * bodyFrame)
                    const Mat bm = Mul(InvAffine(g_weightRel2.body), W2);
                    const auto toBodyWalk = [&](V3 d) { return V3{-(d.x * c - d.z * s), d.y, -(d.x * s + d.z * c)}; };
                    const V3 p = Scale(toBodyWalk(Sub(Pos(bm), headM)), 1.0f / upmM);
                    const V3 a0 = Norm(toBodyWalk(V3{bm.m[0], bm.m[1], bm.m[2]})), a1 = Norm(toBodyWalk(V3{bm.m[4], bm.m[5], bm.m[6]})),
                             a2 = Norm(toBodyWalk(V3{bm.m[8], bm.m[9], bm.m[10]}));
                    const Q4 q = QFromAxes(a0, a1, a2);
                    const float body[3] = {p.x - in.handOffset[0], p.y - in.handOffset[1], p.z - in.handOffset[2]}, rot[4] = {q.x, q.y, q.z, q.w};
                    weight::SetBackBody2(body, rot);
                    if (in.weight.afterDrop == 2)   // not drawn at all: the bone shrunk to a point, as the first's
                        for (int rr = 0; rr < 3; ++rr)
                            for (int cc = 0; cc < 3; ++cc) W2.m[rr * 4 + cc] *= 0.001f;
                }
                newW[b2] = W2;
                second2B = b2;   // (set again after the hands are laid: an arm reached again carries its wrist's bone - the second's - back into that hand)
                second2W = W2;
                std::vector<char> fromWeapon(n, 0);
                fromWeapon[b2] = 1;
                Propagate(h, oldW, newW, fromWeapon, b2);
            }
        } else {
            g_weightRel2.ok = false;
        }
        ReleaseSRWLockExclusive(&g_weightLock);
        if (onBack) {   // the body frame as drawn, in BodyWalk's frame (ToModel undone): W = r.body * bodyFrame
            const Mat bm = Mul(InvAffine(r.body), W);
            const auto toBodyWalk = [&](V3 d) { return V3{-(d.x * c - d.z * s), d.y, -(d.x * s + d.z * c)}; };
            const V3 p = Scale(toBodyWalk(Sub(Pos(bm), headM)), 1.0f / upmM);
            const V3 a0 = Norm(toBodyWalk(V3{bm.m[0], bm.m[1], bm.m[2]})), a1 = Norm(toBodyWalk(V3{bm.m[4], bm.m[5], bm.m[6]})),
                     a2 = Norm(toBodyWalk(V3{bm.m[8], bm.m[9], bm.m[10]}));
            const Q4 q = QFromAxes(a0, a1, a2);
            const float body[3] = {p.x - in.handOffset[0], p.y - in.handOffset[1], p.z - in.handOffset[2]}, rot[4] = {q.x, q.y, q.z, q.w};
            weight::SetBackBody(body, rot);
        }
        // after_drop 2: not drawn at all - the attach bone is shrunk to a point, as the game shrinks one that holds nothing ([debug] shrink_bone's way)
        if ((wo.stowed && in.weight.afterDrop == 2) || wo.hidden)
            for (int rr = 0; rr < 3; ++rr)
                for (int cc = 0; cc < 3; ++cc) W.m[rr * 4 + cc] *= 0.001f;
        // The matrix a wrist's attach bone is given for the weapon W. The slot's bone IS W. The game lays a two-handed weapon along the LINE from one attach bone to the other (both hands
        // on the haft: docs/HANDOVER.md, 10:25-10:30 - and that line is the weapon's own long axis and length): with the two bones in ONE point that line has no length and the weapon
        // comes out squashed flat along its Y - the staff drawn as a disc, though every bone matrix is a proper one. So the other bone goes [hands] carry_cm along the weapon's own axis
        // ([hands] carry_axis), as it always did for a weapon in hand - the weight block had stopped that (2c95c81), and the flat staff followed. [weight] attach_apart off: one point, as that
        // build had it. The other wrist's bone is a mirror on a left hand: the item drawn from a mirror-less matrix is inside out.
        const auto attachFor = [&](int side, const Mat& Wm) {
            Mat m = Wm;
            if (side == slot || h.weapon[side] < 0) return m;
            if ((boneguard::Det(oldW[h.weapon[side]].m) < 0.0) != (boneguard::Det(Wm.m) < 0.0))
                for (int c2 = 0; c2 < 3; ++c2) m.m[c2] = -m.m[c2];
            if (in.weight.attachApart) {
                const int ax = std::clamp(std::abs(in.carryAxis), 1, 3) - 1;
                const V3 dir = Norm(V3{Wm.m[ax * 4], Wm.m[ax * 4 + 1], Wm.m[ax * 4 + 2]});
                const float k = (in.carryAxis < 0 ? -1.0f : 1.0f) * in.carryM * upmM;
                m.m[12] += dir.x * k; m.m[13] += dir.y * k; m.m[14] += dir.z * k;
            }
            return m;
        };
        {   // what the game's own two attach bones look like (for the log: one line for each weapon type) - the data the gap is judged against
            static int toldType = -1;
            if (toldType != in.weaponType && h.weapon[slot ^ 1] >= 0 && in.bothAttach) {
                toldType = in.weaponType;
                const Mat& a = oldW[h.weapon[slot]];
                const int ax = std::clamp(std::abs(in.carryAxis), 1, 3) - 1;
                const V3 d = Sub(Pos(oldW[h.weapon[slot ^ 1]]), Pos(a)), dir = Norm(V3{a.m[ax * 4], a.m[ax * 4 + 1], a.m[ax * 4 + 2]});
                const float cm = 100.0f / std::max(0.01f, upmM), len = Len(d) * cm;
                const float off = len > 0.01f ? acosf(std::clamp(std::fabs(Dot(Norm(d), dir)), 0.0f, 1.0f)) * 57.2958f : 0.0f;
                char b[260];
                snprintf(b, sizeof b, "weight - the game's own attach bones: the other wrist's is %.1f cm from the slot's, its line %.0f deg off the weapon's axis; ours are kept %.0f cm apart along that axis (the game lays a two-handed weapon along the line between them: one point = flat) [weight] attach_apart %s",
                         len, off, in.carryM * 100.0f, in.weight.attachApart ? "on" : "off");
                weight::Say(b);
            }
        }
        for (int side = 0; side < 2; ++side) {
            const int b = h.weapon[side];
            if (b < 0 || (side != slot && (!in.bothAttach || wo.second))) continue;   // the other wrist's own (a shield; a weapon in each hand: the second weapon) stays
            newW[b] = attachFor(side, W);
            std::vector<char> fromWeapon(n, 0);
            fromWeapon[b] = 1;
            Propagate(h, oldW, newW, fromWeapon, b);
        }
        // [weight] grip_fit (vr/grip_fit.h): the hands on the weapon are laid on the shaft as it is DRAWN - the weapon bone just set, its tip axis through its origin (the
        // line the debug draws red). Per HAND (right, left - not a role): the user's live corrections in the shaft's own frame at its grasp. A hand that TOOK the weapon (it
        // closed its fist on the shaft: grabs > 0 - the second hand, a hand that took it over from the other, the game's hand taking it up again) is laid on the shaft: its palm's
        // middle onto the axis, the knuckle line along it, and - [weight] grip_fit_like_base - sitting on it the way the game's own grip has the base hand (that hand's offset from
        // the axis and lean, measured below), then its own corrections. The base hand (the game's hand as placed, grabs 0) is the game's pose, only its corrections act. Only the
        // hand moves - the arm is reached to it again and its fingers follow; the weapon is set again after, so it is where it was (the relation to each hand was taken above,
        // from the hand as the controller has it: the sliders never shift the weapon).
        const bool libGrip = wo.gripLib && g_gripHandStep.load() != nullptr && !freeStaff;   // [weight] grip_model 1: the grip library lays the hands on the shaft
        if ((in.weight.gripFit || libGrip) && !wo.free && !onBack) {
            const int ax = std::abs(in.weight.tipAxis) - 1;
            const float tipSign = in.weight.tipAxis < 0 ? -1.0f : 1.0f;
            const gripfit::Shaft gs = ax >= 0 && ax <= 2
                ? gripfit::Shaft{{W.m[12], W.m[13], W.m[14]}, {W.m[ax * 4] * tipSign, W.m[ax * 4 + 1] * tipSign, W.m[ax * 4 + 2] * tipSign}}
                : gripfit::Shaft{};
            const float ucm = upmM * 0.01f;
            // the drawn hand's own frame (wrist, palm's middle, the knuckles' lines), as Fist reads it
            const auto handGeom = [&](int side) {
                const int k = h.hand[side], mk = h.mid[side], ik = h.ind[side], pk = h.pnk[side];
                const V3 wristP = Pos(newW[k]), midP = Pos(newW[mk]);
                const V3 point = Sub(midP, wristP), line = Sub(Pos(newW[ik]), Pos(newW[pk]));
                const V3 palm = Scale(Add(wristP, midP), 0.5f), out = Scale(Cross(point, line), side == 0 ? -1.0f : 1.0f);   // out of the palm, as Fist has it
                gripfit::Hand gh;
                gh.wrist = {wristP.x, wristP.y, wristP.z};
                gh.palm = {palm.x, palm.y, palm.z};
                gh.point = {point.x, point.y, point.z};
                gh.knuckles = {line.x, line.y, line.z};
                gh.out = {out.x, out.y, out.z};
                gh.side = side;
                return gh;
            };
            // the base grip's relation to the shaft, as last measured for this weapon (cm and degrees, the right hand's convention)
            gripfit::Tuning baseRel;
            gripfit::AxisInHand baseAxis;
            bool baseKnown = false;
            AcquireSRWLockShared(&g_weightLock);
            if (g_weightRel.baseOk && g_weightRel.baseType == in.weaponType) { baseRel = g_weightRel.base; baseAxis = g_weightRel.baseAxis; baseKnown = true; }
            ReleaseSRWLockShared(&g_weightLock);
            bool moved = false;
            // the base hand as it is drawn now: how it sits on the shaft (the palm's offset across and up the hand, the lean of its knuckle line) - what the hands that take it copy
            const auto measureBase = [&](int side) {
                bool okRel = false;
                const gripfit::Tuning rel = gripfit::InCm(gripfit::Relation(handGeom(side), gs, &okRel), ucm);
                if (okRel && std::fabs(rel.off[1]) < 20.0f && std::fabs(rel.off[2]) < 20.0f) {   // (a hand 20 cm off the axis is no base grip)
                    baseRel = rel;
                    baseAxis = gripfit::AxisOf(handGeom(side), gs, upmM);
                    baseKnown = true;
                    AcquireSRWLockExclusive(&g_weightLock);
                    g_weightRel.base = rel;
                    g_weightRel.baseAxis = baseAxis;
                    g_weightRel.baseOk = true;
                    g_weightRel.baseType = in.weaponType;
                    ReleaseSRWLockExclusive(&g_weightLock);
                }
            };
            // [weight] grip_model 1: the hand the grip library lays on the shaft - the hand as the weight model has it and the shaft as drawn go in, the hand to lay comes out as the
            // same kind of fit (a turn about the palm and a shift) and its fingers wait in libGripRes for the finger stage. nullptr when it did; else why not (the hand is then ours).
            const auto libFit = [&](int side, const GripPoseKept& kept, gripfit::Out* fo) -> const char* {
                const gripfit::Hand gh = handGeom(side);
                grippose::M3 e;
                if (!HandFrameAt(h, side, newW, &e)) return "no hand frame";
                const grippose::M3 ep = griprig::ProperFrame(e, side);
                const auto toM = [&](float x, float y, float z) { return D2rgV3{(double)((x - headM.x) / upmM), (double)((y - headM.y) / upmM), (double)((z - headM.z) / upmM)}; };
                D2rgHandIn hin{};
                hin.holds = 1;
                hin.mode = wo.gripMode[side];
                hin.handPos = toM(gh.palm.x, gh.palm.y, gh.palm.z);
                hin.handRot = griprig::QuatOfRows(ep);
                hin.roll = wo.gripRoll[side];
                hin.flags = D2RG_HAND_FACES_AXIS;
                // The shaft against the hand (interface version 2): the way the game's own grip has it when the base hand has been measured on this weapon (the lean of the knuckles, the axis
                // under the fingers - the hand's frame is the library's proper one: its second axis runs the other way on a right hand), else the palm's middle ON the axis, as the grip fit lays
                // a hand with no base to copy. A library of version 1 is given neither (the loader clears them) and lays the palm's surface on the wood's, palmDepth below.
                hin.hasAxisInHand = 1;
                if (baseKnown && baseAxis.ok) {
                    const double ys = side == 0 ? -1.0 : 1.0;
                    hin.lineInHand = {baseAxis.line.x, ys * baseAxis.line.y, baseAxis.line.z};
                    hin.axisInHand = {baseAxis.toAxis.x, ys * baseAxis.toAxis.y, baseAxis.toAxis.z};
                }
                const V3 dir = Norm(V3{gs.dir.x, gs.dir.y, gs.dir.z});
                const int upAxis = (ax + 1) % 3;
                hin.shaft.at = toM(gs.at.x, gs.at.y, gs.at.z);
                hin.shaft.dir = {dir.x, dir.y, dir.z};
                hin.shaft.up = {W.m[upAxis * 4], W.m[upAxis * 4 + 1], W.m[upAxis * 4 + 2]};
                hin.shaft.butt = wo.shaftButtT;
                hin.shaft.tip = wo.shaftTipT;
                hin.shaft.nPieces = std::min(wo.nPieces, D2RG_MAX_PIECES);
                for (int i = 0; i < hin.shaft.nPieces; ++i) hin.shaft.pieces[i] = {wo.pieceT0[i], wo.pieceT1[i], wo.pieceR[i]};
                const V3 palm{gh.palm.x, gh.palm.y, gh.palm.z};
                const V3 rel = Sub(palm, V3{gs.at.x, gs.at.y, gs.at.z});
                const float along = Dot(rel, dir);
                if (baseKnown) {   // the palm's depth: how far the game's own grip has the palm's middle off the axis (across the palm), less the wood's radius there
                    const float tp = along / upmM;
                    float rad = 0.015f, best = -1.0f;
                    for (int i = 0; i < hin.shaft.nPieces; ++i)
                        if (tp >= wo.pieceT0[i] && tp <= wo.pieceT1[i]) best = std::max(best, wo.pieceR[i]);
                    if (best >= 0.0f) rad = best;
                    else if (hin.shaft.nPieces > 0) rad = wo.pieceR[0];
                    const float depth = -baseRel.off[2] * 0.01f - rad;
                    if (depth > 0.004f && depth < 0.05f) hin.palmDepth = depth;
                }
                LibGripResult res;
                if (const char* why = LibGripRun(h, side, in, kept, newW, upmM, hin, &res)) return why;
                const V3 pm = Add(headM, Scale(V3{(float)res.out.handPos.x, (float)res.out.handPos.y, (float)res.out.handPos.z}, upmM));
                {   // said once for each hand, weapon and way of laying: where the library put the palm against the shaft (what a test of the hold is judged by)
                    static uint32_t toldKey[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
                    const bool fromBase = baseKnown && baseAxis.ok;
                    const GripAbiFn abiFn = g_gripAbi.load();
                    const int abi = abiFn ? abiFn() : D2RG_ABI_VERSION;
                    const uint32_t key = ((uint32_t)in.weaponType << 4) | (fromBase ? 2u : 0u) | (abi >= 2 ? 1u : 0u);
                    if (toldKey[side] != key) {
                        toldKey[side] = key;
                        const V3 relNew = Sub(pm, V3{gs.at.x, gs.at.y, gs.at.z});
                        const float cmNow = Len(Sub(relNew, Scale(dir, Dot(relNew, dir)))) / ucm;
                        char b[300];
                        if (abi < 2)
                            snprintf(b, sizeof b, "grip library - the %s hand is laid by its palm's surface on the wood's (interface 1): the palm's middle stands %.1f cm from the axis", side ? "left" : "right", cmNow);
                        else if (fromBase)
                            snprintf(b, sizeof b, "grip library - the %s hand is laid as the game's own grip has the shaft: %.1f cm toward the fingertips and %.1f cm out of the palm from the palm's middle, the knuckles %.0f deg off it; the palm's middle stands %.1f cm from the axis",
                                     side ? "left" : "right", baseAxis.toAxis.x * 100.0f, baseAxis.toAxis.z * 100.0f, acosf(std::clamp(std::fabs(baseAxis.line.y), 0.0f, 1.0f)) * 57.2958f, cmNow);
                        else
                            snprintf(b, sizeof b, "grip library - the %s hand is laid with the palm's middle on the shaft's axis (no base grip measured on this weapon yet): it stands %.1f cm from it", side ? "left" : "right", cmNow);
                        weight::Say(b);
                    }
                }
                const grippose::M3 et = griprig::RowsOfQuat(res.out.handRot);
                const grippose::M3 rr = grippose::Mul(grippose::Transpose(ep), et);
                gripfit::Out o;
                o.ok = true;
                o.pivot = gh.palm;
                for (int i = 0; i < 9; ++i) o.r.m[i] = rr.m[i];
                o.shift = {pm.x - palm.x, pm.y - palm.y, pm.z - palm.z};
                const float tHold = (float)res.out.t * upmM;
                o.grasp = {gs.at.x + dir.x * tHold, gs.at.y + dir.y * tHold, gs.at.z + dir.z * tHold};
                o.anchor = {pm.x, pm.y, pm.z};
                const V3 perp = Sub(rel, Scale(dir, along));
                o.offAxis = Len(perp);
                const V3 kn = Norm(V3{gh.knuckles.x, gh.knuckles.y, gh.knuckles.z});
                o.alignDeg = acosf(std::clamp(fabsf(Dot(kn, dir)), 0.0f, 1.0f)) * 57.2957795f;
                {   // the user's corrections for this hand (right_grip_* / left_grip_*: the same keys, the same frame and signs as for ours) on top of where the library laid it
                    gripfit::Tuning user;
                    for (int i = 0; i < 3; ++i) { user.off[i] = in.weight.gripHand[side][i]; user.rot[i] = in.weight.gripHand[side][3 + i]; }
                    o = gripfit::WithCorrections(gh, gs, o, user, ucm);
                }
                *fo = o;
                libGripRes[side] = std::move(res);
                return nullptr;
            };
            for (int pass = 0; pass < 2 && ax >= 0 && ax <= 2; ++pass) {   // the base hand first (its relation is what the others copy), then the hands that took it
                for (int side = 0; side < 2; ++side) {
                    const int k = h.hand[side], mk = h.mid[side], ik = h.ind[side], pk = h.pnk[side];
                    if (k < 0 || mk < 0 || ik < 0 || pk < 0 || !wo.holds[side] || !(wo.on01[side] > 0.0f) || !(in.handsValid & (1u << side))) continue;
                    const bool isBase = wo.grabs[side] == 0;
                    if ((pass == 0) != isBase) continue;
                    gripfit::Tuning user;
                    for (int i = 0; i < 3; ++i) { user.off[i] = in.weight.gripHand[side][i]; user.rot[i] = in.weight.gripHand[side][3 + i]; }
                    gripfit::Plan plan = gripfit::PlanFor(wo.grabs[side], user, in.weight.gripLikeBase, baseKnown ? &baseRel : nullptr);
                    gripfit::Out fo;
                    bool libDone = false;   // the grip library laid this hand
                    if (libGrip) {
                        if (isBase && wo.on01[side] >= 1.0f) measureBase(side);   // before it is laid: how the game's own grip sits on the shaft (the library's palm depth comes from it)
                        GripPoseKept kept;
                        AcquireSRWLockShared(&g_gripPoseLock); kept = g_gripPose; ReleaseSRWLockShared(&g_gripPoseLock);
                        static const char* told[2] = {nullptr, nullptr};   // why the library could not lay this hand, said once for each reason
                        const char* why = libFit(side, kept, &fo);
                        if (why) {
                            if (told[side] != why) {
                                char b[260];
                                snprintf(b, sizeof b, "grip library - the %s hand is ours this time: %s", side ? "left" : "right", why);
                                weight::Say(b);
                                told[side] = why;
                            }
                            libGripRes[side] = LibGripResult{};
                        } else {
                            libDone = true;
                            told[side] = nullptr;
                            plan.act = true;
                            plan.mode = gripfit::Mode{true, true};
                            plan.likeBase = false;
                            plan.tune = gripfit::Tuning{};
                        }
                    }
                    if (plan.act) {
                        const gripfit::Hand gh = handGeom(side);
                        if (!libDone) fo = gripfit::Solve(gh, gs, plan.tune, plan.mode, ucm);
                        if (!fo.ok) continue;
                        const float t01 = std::clamp(wo.on01[side], 0.0f, 1.0f), e = t01 * t01 * (3.0f - 2.0f * t01);   // eased on with the hand's own ease onto the weapon
                        const V3 wristP = Pos(newW[k]);
                        const gripfit::V3 w2 = gripfit::Add(fo.pivot, gripfit::Add(gripfit::Apply(gripfit::Sub(gh.wrist, fo.pivot), fo.r), fo.shift));
                        const Mat before = newW[k];
                        Mat target = before;
                        for (int i = 0; i < 3; ++i) {
                            const gripfit::V3 r = gripfit::Apply({before.m[i * 4], before.m[i * 4 + 1], before.m[i * 4 + 2]}, fo.r);
                            target.m[i * 4] = r.x; target.m[i * 4 + 1] = r.y; target.m[i * 4 + 2] = r.z;
                        }
                        Reach(h, side, Add(wristP, Scale(Sub(V3{w2.x, w2.y, w2.z}, wristP), e)), turn, c, s, baseW, newW);
                        Mat fitted = BlendRot(before, target, e);
                        fitted.m[12] = newW[k].m[12]; fitted.m[13] = newW[k].m[13]; fitted.m[14] = newW[k].m[14];
                        newW[k] = fitted;
                        std::vector<char> fromHand(n, 0);
                        fromHand[k] = 1;
                        Propagate(h, oldW, newW, fromHand, k);   // the fingers (and the wrist's weapon bone, set again below)
                        moved = true;
                        // for the debug and the log: the grasp point and where the palm really stands now (the arm's reach may fall short of it)
                        const V3 palmNow = Scale(Add(Pos(newW[k]), Pos(newW[mk])), 0.5f);
                        const auto back = [&](V3 p) {   // model space -> the head's turn-only frame, metres from the eye (as GrabLine)
                            const V3 d = Scale(Sub(p, headM), 1.0f / upmM);
                            const V3 q = {d.x * c - d.z * s, d.y, d.x * s + d.z * c};
                            return V3{-q.x, q.y, -q.z};
                        };
                        const V3 g = back(V3{fo.grasp.x, fo.grasp.y, fo.grasp.z}), p = back(palmNow);
                        GripMarkOut gm{{g.x, g.y, g.z}, {p.x, p.y, p.z}, GetTickCount64()};
                        AcquireSRWLockExclusive(&g_gripMarkLock); g_gripMark[side] = gm; ReleaseSRWLockExclusive(&g_gripMarkLock);
                        weight::GripFitNote note;
                        note.side = side;
                        note.taker = !isBase;
                        note.grabs = wo.grabs[side];
                        note.settled = wo.on01[side] >= 1.0f;
                        note.offAxisCm = fo.offAxis / ucm;
                        note.alignDeg = fo.alignDeg;
                        note.missCm = Len(Sub(palmNow, V3{fo.anchor.x, fo.anchor.y, fo.anchor.z})) / ucm;
                        note.snapped = plan.mode.snap;
                        note.aligned = plan.mode.align;
                        note.likeBase = plan.likeBase;
                        for (int i = 0; i < 3; ++i) { note.tune[i] = user.off[i]; note.tune[3 + i] = user.rot[i]; }
                        if (plan.likeBase) for (int i = 0; i < 3; ++i) { note.like[i] = baseRel.off[i]; note.like[3 + i] = baseRel.rot[i]; }
                        weight::NoteGripFit(note);
                    }
                    if (isBase && wo.on01[side] >= 1.0f) measureBase(side);   // the base hand as it is drawn now (its corrections in; the library's too, with its answer laid): how it sits on the shaft - what the hands that take it copy
                }
            }
            if (moved)
                for (int side = 0; side < 2; ++side) {
                    const int b = h.weapon[side];
                    if (b < 0 || (side != slot && (!in.bothAttach || wo.second))) continue;   // (a weapon in each hand: the other wrist's bone is the second weapon's - never the first's)
                    newW[b] = attachFor(side, W);   // where it was
                    std::vector<char> fromWeapon(n, 0);
                    fromWeapon[b] = 1;
                    Propagate(h, oldW, newW, fromWeapon, b);
                }
            if (second2B >= 0) {   // [weight] second_lets_go: the second weapon let go, or in the first's hand, where its body is - again, after the arms were reached
                newW[second2B] = second2W;
                std::vector<char> fromWeapon(n, 0);
                fromWeapon[second2B] = 1;
                Propagate(h, oldW, newW, fromWeapon, second2B);
            }
        }
        if (hidden2B >= 0) {   // [hands] throw_left_hand: thrown by the game - the hand empty, the second weapon not drawn wherever its body is (a point, as the first's)
            for (int rr = 0; rr < 3; ++rr)
                for (int cc = 0; cc < 3; ++cc) newW[hidden2B].m[rr * 4 + cc] *= 0.001f;
            std::vector<char> fromWeapon(n, 0);
            fromWeapon[hidden2B] = 1;
            Propagate(h, oldW, newW, fromWeapon, hidden2B);
        }
        // The weight model's shaft IS the drawn weapon bone: W, just set on both attach bones, measured against this body frame on every pose it is held (vr/weapon_shaft.h) and
        // told back (weight::SetDrawnShaft) - whichever wrist the game would hang the item on, whichever hand carries it. So the red line, the line both grasp points lie
        // on and hit by touch's capsule (built from the same bone, with the same ends) are one line. (It was measured at the end of the pose from the bone of the game's
        // hand slot only while that slot's bone was the drawn one: walking, the game moves a two-handed weapon to the other wrist, and the model's line then stood still
        // while the weapon moved. The old carry of the other attach bone, below, no longer runs for a weapon the weight model holds: it shifted that bone along the shaft.)
        // (Not off a matrix that is not the weapon's own: hidden while the game throws - squeezed to a point - or set right from the game's shrunk bone, whose rows
        // come back with their axes turned: a javelin's line measured off it lay 180 degrees off the drawn one, its 1.8 m swung through the other weapon of the pair
        // and back after every throw - the headset 2026-10-28, «the axe's blade goes straight through the javelin's shaft». The last good measurement stands.)
        if (!wo.free && !wo.hidden && wFault == boneguard::Fault::None) {
            if (in.weight.shaftTipM + in.weight.shaftButtM > 0.01f) {
                const weaponshaft::Line ln = weaponshaft::Relative(W.m, bodyM.m, upmM, in.weight.tipAxis, in.weight.shaftTipM, in.weight.shaftButtM);
                if (ln.ok) {
                    const float a[3] = {(float)ln.at[0], (float)ln.at[1], (float)ln.at[2]}, d[3] = {(float)ln.dir[0], (float)ln.dir[1], (float)ln.dir[2]};
                    weight::SetDrawnShaft(a, d, (float)ln.butt, (float)ln.tip, (float)ln.boneAlong);
                    drawnHead(W, bodyM, 0, &weight::SetDrawnHeadParts);   // [weight] dual_collide: its head's own volume, as drawn
                } else {
                    weight::NoteShaftUnmeasured("the weapon bone could not be measured against the body's frame");
                }
            } else {
                weight::NoteShaftUnmeasured("the blade's extents are not known (hit by touch's blade shape gave none)");
            }
        }
        weightDrew = true;
    }

    // A staff is held in both hands, but the game hangs it on one wrist: with
    // each hand on its own controller the staff went with that one and passed
    // beside the other. It is turned about its grip so that it runs through the
    // other hand as well - where the game's own animation has that hand on it.
    //
    // [hands] staff_free_left turns that round: the right hand holds the staff
    // fast (placed above, with the grip and Weapon Adjust), nothing turns it
    // toward the left. The left hand is free on its controller; with the grip
    // held it goes onto the shaft - in the game's grip round it, slid along it
    // to where the controller is - and eases back off when let go.
    bool leftOnStaff = false;   // its fingers keep the game's grip round the shaft, no fist
    if (!(freeStaff && gun && in.xbowLatch)) XbowLatchReset();   // [hands] xbow_grip_latch: no crossbow in the gun's hold - a fresh latch next time
    if (freeStaff) {
        // the support hand: the left one, or with the crossbow passed into the left hand ([hands] xbow_handover) the right one
        const int sp = gun ? holdSide ^ 1 : 1;
        const int foreOther = staffSide & 1;   // the game's own fore-grip hand: the one it hangs the crossbow on
        const int wb = h.weapon[staffSide], lk = h.hand[sp], rk = h.hand[sp ^ 1];
        Mat staff = newW[wb];
        // The shaft is one of the staff bone's own axes - the one nearest the
        // line between the palms in the game's grip (that line itself ran
        // askew: the animation's right hand does not quite sit on the shaft,
        // and the left hand slid along a phantom). It runs through the bone's
        // origin: where the game hangs the staff, in the middle of the fist.
        const V3 inR = {grip.palm[0][0], grip.palm[0][1], grip.palm[0][2]}, inL = {grip.palm[1][0], grip.palm[1][1], grip.palm[1][2]};
        const V3 v = Sub(inL, inR);   // staff frame, from the right palm toward the left one
        const float av[3] = {fabsf(v.x), fabsf(v.y), fabsf(v.z)};
        int ai = av[0] >= av[1] && av[0] >= av[2] ? 0 : av[1] >= av[2] ? 1 : 2;
        float sign = (ai == 0 ? v.x : ai == 1 ? v.y : v.z) < 0.0f ? -1.0f : 1.0f;
        if (in.shaftAxis >= -3 && in.shaftAxis <= 3 && in.shaftAxis != 0) { ai = abs(in.shaftAxis) - 1; sign = in.shaftAxis < 0 ? -1.0f : 1.0f; }
        const V3 shaftInStaff = {ai == 0 ? sign : 0.0f, ai == 1 ? sign : 0.0f, ai == 2 ? sign : 0.0f};
        V3 axis = Norm(RotOnly(shaftInStaff, staff));   // from the right hand toward the left one
        V3 origin = Pos(staff);
        // A crossbow: the line is its stock as the model lies, from where the right hand
        // holds it - the left hand goes on it, the bolt along it (vrcam HandRay): the
        // model and what shoots and is taken are one ("they must coincide", 2026-10-05).
        if (gun) { axis = gunStock; origin = gunAt; }
        // [hands] xbow_support_model / [debug] xbow_barrel_line: the crossbow's barrel as drawn (NoteCrossbowBarrel: its bolt_helper, on the
        // attach bone as placed above) and the left palm's place under it (xbowgrip::Support). Only the left hand goes there: axis and
        // origin stay the shot's line (g_staff.dir below, vrcam HandRay) - the bolt flies as before.
        bool onBarrel = false;
        V3 barrelAt{}, barrelDir{};
        bool haveBarrelDir = false;
        V3 barrelUp{}, barrelCenter{};
        // [hands] xbow_support_game: learn the game's own fore-grip from its pose (oldW) while it holds the crossbow in both hands
        XbowForeGrip& fore = g_xbowFore;
        static GripPoseKept foreFingers;   // [hands] xbow_support_close: the game's support hand's fingers on the fore-grip (grip_pose.h)
        static std::vector<Mat> foreRel;    // the same, each finger bone in that hand's own frame (our support hand on the game's side)
        bool onFore = false;
        if (gun) {
            const uint64_t key = ((uint64_t)HeroGen() << 32) | (uint32_t)in.weaponType;
            if (fore.key != key) { fore = XbowForeGrip{}; fore.key = key; foreFingers = GripPoseKept{}; foreRel.clear(); }
            // `other`: the game's fore-grip hand (the one it hangs the crossbow on) - learned for our support hand; `holder`: its trigger hand
            const int other = staffSide & 1, holder = other ^ 1;
            const int hh = h.hand[holder], ho = h.hand[other];
            if (fore.n < kForeGripSamples && !in.attacking && hh >= 0 && ho >= 0 && h.mid[0] >= 0 && h.mid[1] >= 0 && h.ind[other] >= 0 && h.pnk[other] >= 0) {
                const V3 ph = Scale(Add(Pos(oldW[hh]), Pos(oldW[h.mid[holder]])), 0.5f), po = Scale(Add(Pos(oldW[ho]), Pos(oldW[h.mid[other]])), 0.5f);
                const Mat att = Mul(oldW[wb], InvAffine(oldW[ho]));
                const bool shown = Len({att.m[0], att.m[1], att.m[2]}) > 0.7f;   // the crossbow on the wrist the game hangs it on, not hidden
                if (shown && Len(Sub(ph, po)) <= 1.0f) {   // the game's hold: both hands on it
                    const Mat toStaff = InvAffine(oldW[wb]);
                    fore.sum = Add(fore.sum, Through(toStaff, po));
                    fore.trigSum = Add(fore.trigSum, Through(toStaff, ph));
                    ++fore.n;
                    fore.palm = Scale(fore.sum, 1.0f / (float)fore.n);
                    fore.trigPalm = Scale(fore.trigSum, 1.0f / (float)fore.n);
                    // the palm's facing: out of the palm = (wrist -> middle knuckle) x (little -> index knuckle), away on the right hand
                    const V3 pt = Sub(Pos(oldW[h.mid[other]]), Pos(oldW[ho])), ac = Sub(Pos(oldW[h.ind[other]]), Pos(oldW[h.pnk[other]]));
                    const V3 out = Scale(Norm(Cross(pt, ac)), other == 0 ? -1.0f : 1.0f);
                    fore.facing = Norm(RotOnly(out, toStaff));
                    fore.across = Norm(RotOnly(ac, toStaff));
                    GripPoseKept fingers;   // its fingers as the game closes them on the fore-grip (the last sample of the hold)
                    if (TakeGripPose(h, other, oldW, in.weaponType, &fingers)) foreFingers = std::move(fingers);
                    foreRel.clear();         // and each finger bone against that hand, for our hand on the same side
                    for (const Hero::Finger& f : h.fingers[other])
                        for (const int bone : f.bones) foreRel.push_back(Mul(oldW[bone], InvAffine(oldW[ho])));
                }
            }
            onFore = in.xbowSupportGame && fore.n >= kForeGripSamples;
            if (onFore) { barrelAt = Through(staff, fore.palm); onBarrel = true; }
        }
        // [hands] xbow_aim_barrel (from bearstonem/d2r-pcvr: vr/crossbow.cpp, steamvr/crossbow_pose.h - the full barrel direction of the
        // held model's bolt_helper): the shot goes along the barrel as the renderer draws it, fresh within 100 ms of the pose like his
        // (Fresh); else - and with the tick off - along the hand-set shot line ([hands] xbow_line_*). Only the direction the game's own
        // attack is aimed by (g_staff.dir -> vrcam HandRay -> the aim point); the game's missile, damage, ammunition and cadence are its own.
        bool aimBarrel = false;
        V3 aimDir{};
        V3 muzzle{};            // the front of the stock on the drawn barrel (the debug ray's start)
        bool haveMuzzle = false;
        if (gun && (in.xbowModel || in.xbowBarrelDebug || in.xbowAimBarrel)) {
            const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
            const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
            const ULONGLONG now = GetTickCount64();
            {   // our attach bone in the world, for XbowRendered (the hero's model-to-world, as NotePosedWeapon takes it)
                HeroXform hx; Mat model;
                if (HeroTransform(&hx) && hx.self == self && (hx.parent & 0xFFFFFu) == 0xFFFFFu && SafeCopy(model.m, (const void*)hx.comp, sizeof model)) {
                    PoseModel(model);
                    XbowAttach a;
                    const Mat w = Mul(staff, model);
                    memcpy(a.world.m, w.m, sizeof a.world.m);
                    a.upm = upm; a.gen = HeroGen(); a.at = now;
                    AcquireSRWLockExclusive(&g_gunBoneLock); g_xbowAttach = a; ReleaseSRWLockExclusive(&g_gunBoneLock);
                }
            }
            AcquireSRWLockShared(&g_gunBoneLock); const XbowBarrelOut rendered = g_xbowRendered, own = g_xbowBarrel; ReleaseSRWLockShared(&g_gunBoneLock);
            // the renderer's placing for the hand; the item's own transform only to draw (orange) when that is not seen. The renderer's barrel
            // in the attach bone's frame is the crossbow's own (rigid): once seen for this weapon and hero model it is kept - a render frame
            // or two without it (test-67's log, 13:18:59.83) flipped the hand and the aim to the shot line for 30 ms.
            XbowBarrelOut& heldRendered = g_xbowHeldBarrel;
            uint64_t& heldKey = g_xbowHeldKey;
            const uint64_t barrelKey = ((uint64_t)HeroGen() << 32) | (uint32_t)in.weaponType;
            if (rendered.at && now - rendered.at <= 300 && rendered.gen == HeroGen()) { heldRendered = rendered; heldKey = barrelKey; }
            const bool fromRenderer = heldKey == barrelKey && heldRendered.at != 0;
            XbowBarrelOut b = fromRenderer ? heldRendered : own;
            if (fromRenderer) b.at = now;
            xbowgrip::M placed;
            memcpy(placed.m, staff.m, sizeof placed.m);
            xbowgrip::Line line;
            const bool barrelOk = b.at && now - b.at <= 500 && b.gen == HeroGen() && xbowgrip::FromBolt(xbowgrip::Mul(b.inAttach, placed), &line);
            if (barrelOk) {
                // the place by the crossbow alone: 6 cm behind the front of the stock (where the limbs meet it), 7 below the barrel, + the adjustments
                const xbowgrip::V p = xbowgrip::SupportAt(line, xbowgrip::kBackM + in.xbowBackM, xbowgrip::kDropM + in.xbowDropM, upm);
                if (!onFore) barrelAt = {p.x, p.y, p.z};   // the game's fore-grip, when learned, is the place; else under the drawn barrel
                barrelDir = {line.dir.x, line.dir.y, line.dir.z};
                haveBarrelDir = fromRenderer;
                if (fromRenderer) {
                    barrelUp = {line.up.x, line.up.y, line.up.z};
                    barrelCenter = {line.center.x, line.center.y, line.center.z};
                    // the game's support hand is its RIGHT one: our left takes the fore-grip mirrored across the crossbow's own upright plane
                    if (onFore && sp != foreOther) {
                        const xbowgrip::V side = xbowgrip::Cross(line.dir, line.up);
                        const xbowgrip::V m = xbowgrip::MirrorPoint({barrelAt.x, barrelAt.y, barrelAt.z}, line.center, side);
                        barrelAt = {m.x, m.y, m.z};
                    }
                }
                if (onFore && !fore.told && fromRenderer) {   // how the game's support palm faces the barrel, once a weapon: cup / wrap / over
                    fore.told = true;
                    const V3 upA = Norm(RotOnly({line.up.x, line.up.y, line.up.z}, InvAffine(staff)));   // the barrel's up in the attach bone's frame
                    const float deg = acosf(std::clamp(Dot(fore.facing, upA), -1.0f, 1.0f)) * 57.2957795f;
                    const V3 at = Through(staff, fore.palm);
                    const xbowgrip::V rel = xbowgrip::Sub({at.x, at.y, at.z}, line.center);
                    const float along = xbowgrip::Dot(rel, line.dir) / upm * 100.0f, below = -xbowgrip::Dot(rel, line.up) / upm * 100.0f;
                    char t[300];
                    snprintf(t, sizeof t, "crossbow fore-grip (the game's own two-handed hold, %d samples): the palm's middle %.1f cm %s the front of the stock, %.1f cm %s the barrel; "
                             "the palm faces %.0f deg from the barrel's up - %s", fore.n, std::fabs(along), along < 0.0f ? "behind" : "ahead of",
                             std::fabs(below), below >= 0.0f ? "below" : "above", deg,
                             deg < 50.0f ? "it cups the stock from below" : deg < 130.0f ? "it wraps the stock from the side" : "it holds it from above");
                    XbowNote(t);
                }
                muzzle = {line.center.x, line.center.y, line.center.z};
                haveMuzzle = fromRenderer;
                if (in.xbowAimBarrel && fromRenderer && std::isfinite(barrelDir.x + barrelDir.y + barrelDir.z) && Len(barrelDir) > 0.5f) {
                    aimBarrel = true;
                    aimDir = Norm(barrelDir);
                }
            }
            if (in.xbowAimBarrel) {   // which line the shot goes along, on change (at most every half second)
                static int toldAim = -1;
                static ULONGLONG toldAimAt = 0;
                if ((aimBarrel ? 1 : 0) != toldAim && now - toldAimAt >= 500) {
                    toldAim = aimBarrel ? 1 : 0;
                    toldAimAt = now;
                    XbowNote(aimBarrel ? "crossbow aim (xbow_aim_barrel): along the drawn barrel"
                                       : "crossbow aim (xbow_aim_barrel): the drawn barrel is not fresh (not read, or older than 100 ms) - along the hand-set shot line");
                }
            }
            if (barrelOk) {
                const V3 p = barrelAt;
                // (No longer tied to the shot line: Serg turns that for the shot, 64 deg off the barrel in test-42, and the hand stayed off the
                // barrel. The barrel as the renderer draws it was right there; the 3 s log line still gives the angle between them.)
                onBarrel = onFore || (in.xbowModel && fromRenderer && std::isfinite(p.x + p.y + p.z));
                if (in.xbowBarrelDebug) {   // from the eye in the head's turn-only frame, metres, less the hands' offset (as the bow's vectors)
                    auto back = [&](V3 q) {
                        const V3 d = Scale(Sub(q, head), 1.0f / upm);
                        const V3 m = {d.x * c - d.z * s, d.y, d.x * s + d.z * c};
                        return V3{-m.x - in.handOffset[0], m.y - in.handOffset[1], -m.z - in.handOffset[2]};
                    };
                    const V3 cen = {line.center.x, line.center.y, line.center.z};
                    const V3 A = back(Add(cen, Scale(barrelDir, -0.4f * upm))), B = back(Add(cen, Scale(barrelDir, 0.3f * upm))), P = back(barrelAt);
                    const XbowDebugOut dbg{{A.x, A.y, A.z}, {B.x, B.y, B.z}, {P.x, P.y, P.z}, fromRenderer, now};
                    AcquireSRWLockExclusive(&g_gunBoneLock); g_xbowDebug = dbg; ReleaseSRWLockExclusive(&g_gunBoneLock);
                    // and in the log, every 3 s: the points in the same frame (cm: x right, y up, z back), to lay against the picture
                    static ULONGLONG toldAt = 0;
                    if (now - toldAt >= 3000) {
                        toldAt = now;
                        const V3 C = back(cen), R = back(gunAt), S = back(Add(gunAt, Scale(gunStock, 0.3f * upm))), O = back(Pos(staff));
                        const V3 dirH = Sub(B, A), stockH = Sub(S, R);
                        const float lenD = Len(dirH), lenS = Len(stockH);
                        const float angle = lenD > 1e-6f && lenS > 1e-6f ? acosf(std::clamp(Dot(dirH, stockH) / (lenD * lenS), -1.0f, 1.0f)) * 57.2957795f : -1.0f;
                        const V3 dirA = Norm(RotOnly(barrelDir, InvAffine(staff)));   // the barrel in the attach bone's own axes
                        char t[600];
                        snprintf(t, sizeof t, "crossbow barrel (%s), head frame cm (x right, y up, z back): front of stock %.1f %.1f %.1f, barrel dir %.2f %.2f %.2f "
                                 "(attach bone axes %.2f %.2f %.2f), palm place %.1f %.1f %.1f, right palm %.1f %.1f %.1f, attach bone %.1f %.1f %.1f, "
                                 "shot line dir %.2f %.2f %.2f - barrel vs shot line %.0f deg; scale %.2f",
                                 fromRenderer ? "as the renderer draws it" : "from the item's own transform - NOT how it is drawn",
                                 C.x * 100, C.y * 100, C.z * 100, dirH.x / std::max(lenD, 1e-6f), dirH.y / std::max(lenD, 1e-6f), dirH.z / std::max(lenD, 1e-6f),
                                 dirA.x, dirA.y, dirA.z, P.x * 100, P.y * 100, P.z * 100, R.x * 100, R.y * 100, R.z * 100, O.x * 100, O.y * 100, O.z * 100,
                                 stockH.x / std::max(lenS, 1e-6f), stockH.y / std::max(lenS, 1e-6f), stockH.z / std::max(lenS, 1e-6f), angle, line.scale);
                        XbowNote(t);
                    }
                }
            }
        }
        if (gun && in.xbowBarrelDebug) {   // the shot's ray, live: from the muzzle (else 40 cm ahead of the right palm) along what the aim uses
            const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
            const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
            auto back = [&](V3 q) {   // the barrel's frame: from the eye, head turn only, metres, less the hands' offset
                const V3 d = Scale(Sub(q, head), 1.0f / upm);
                const V3 m = {d.x * c - d.z * s, d.y, d.x * s + d.z * c};
                return V3{-m.x - in.handOffset[0], m.y - in.handOffset[1], -m.z - in.handOffset[2]};
            };
            const V3 dirM = aimBarrel ? aimDir : Norm(axis);   // axis: the hand-set shot line here (the gun's)
            const V3 start = haveMuzzle ? muzzle : Add(gunAt, Scale(Norm(axis), 0.4f * upm));
            const V3 A = back(start), T = back(Add(start, Scale(dirM, upm)));
            V3 D = Sub(T, A);
            const float dl = Len(D);
            if (dl > 1e-6f && std::isfinite(dl)) {
                D = Scale(D, 1.0f / dl);
                // the floor: the user's eye height below the eye (the frame's y is from the eye), less the hands' offset as above
                const float floorY = -std::max(0.8f, in.userHeadM) - in.handOffset[1];
                float len = 8.0f;
                bool floor = false;
                if (D.y < -0.02f) {
                    const float t = (floorY - A.y) / D.y;
                    if (t > 0.0f && t < len) { len = t; floor = true; }
                }
                const V3 B = Add(A, Scale(D, len));
                const XbowRayOut ray{{A.x, A.y, A.z}, {B.x, B.y, B.z}, aimBarrel, floor, GetTickCount64()};
                AcquireSRWLockExclusive(&g_gunBoneLock); g_xbowRay = ray; ReleaseSRWLockExclusive(&g_gunBoneLock);
            }
        }
        const V3 atR = Through(staff, inR);
        const float tR = gun ? 0.0f : Dot(Sub(atR, origin), axis);   // the right palm, along the shaft
        // A crossbow's palms in the game's grip need not lie along its stock: the gap
        // between them is taken whole (along the stock it could be near nothing, and
        // the left hand would never take it).
        float gap = in.gunStock ? Len(Sub(Through(staff, inL), atR))
                                : fabsf(Dot(Sub(Through(staff, inL), origin), axis) - tR);
        // A shaft laid by [hands] *_shaft_axis: the game's left hand is off it, its gap means
        // nothing - the left slides 1.5 x 30 cm either way of the right fist.
        if (in.shaftAxis != 0 && !in.leftHilt && !gun) {
            const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
            gap = 0.3f * std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;
        }
        if (lk >= 0 && (gap > 0.05f || in.leftHilt)) {
            // [hands] staff_grab_cm: how near the shaft (within its length) the
            // left hand has to be for the grip to take the staff.
            bool inReach = false;
            if (in.handsValid & (1u << sp)) {
                const V3 C = ControllerPoint(h, sp, in, newW, c, s);
                const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
                const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
                // a crossbow is taken at its one place only (leftStaffAtM), a sword's hilt
                // about its one place (leftHilt), a staff anywhere along it
                const float t = in.leftStaffAtM >= 0.0f ? tR + in.leftStaffAtM * upm
                              : in.leftHilt ? tR + std::clamp(Dot(Sub(C, origin), axis) - tR, (in.leftHiltAtM - in.leftHiltSlideM) * upm,
                                                              (in.leftHiltAtM + in.leftHiltSlideM) * upm)
                                            : std::clamp(Dot(Sub(C, origin), axis), tR - kStaffReach * gap, tR + kStaffReach * gap);
                inReach = Len(Sub(C, onBarrel ? barrelAt : Add(origin, Scale(axis, t)))) <= in.staffGrabM * upm;
            }
            // [hands] xbow_grip_latch: the crossbow's own latch - a lost left controller frees the hand (it is 1, on the stock, otherwise)
            const float e = gun && in.xbowLatch ? XbowEase(in.grip[sp], inReach, in.fistCap[sp] >= 0.0f, (in.handsValid & (1u << sp)) != 0,
                                                           ((uint64_t)HeroGen() << 48) ^ ((uint64_t)sp << 44) ^ (uint64_t)self, (uint32_t)in.weaponType)
                          : (in.handsValid & (1u << sp)) ? StaffEase(in.grip[sp], inReach, in.fistCap[sp] >= 0.0f) : 1.0f;
            leftOnStaff = e >= 0.5f;
            if (gun) {   // [hands] xbow_handover: the support hand holds it - the holding hand letting go then passes it
                g_xbowSupportHeld = e >= 0.5f;
                g_xbowSupportHeldKey = ((((uint64_t)HeroGen() << 32) | (uint32_t)in.weaponType)) ^ ((uint64_t)(holdSide & 1) << 63);
            }
            // Held in both hands, the staff is held between them: it runs from
            // the right fist through the left controller, and the right wrist's
            // own turn no longer swings it. Turned about the shaft's point in the
            // right fist, the right hand with it (so the fist stays round the
            // shaft; its roll about the shaft is still the controller's).
            // Not a crossbow: it points where the right controller does, the left hand only holds it.
            // Nor a two-handed sword: the hands sit a fist apart on its hilt, and the line between
            // the controllers that close turned the blade with every tremor.
            if (!gun && !in.leftHilt && e > 0.0f && (in.handsValid & (1u << sp)) && rk >= 0) {
                const V3 pR = Add(origin, Scale(axis, tR));
                V3 toL = Sub(ControllerPoint(h, sp, in, newW, c, s), pR);
                if (Dot(toL, axis) < 0.0f) toL = Scale(toL, -1.0f);   // the left hand past the right one: the same line
                if (Len(toL) > 0.3f * gap) {
                    const Mat R = RotBetween(axis, Add(Scale(axis, 1.0f - e), Scale(Norm(toL), e)));
                    auto about = [&](Mat& w) {   // turned by R about pR
                        const V3 q = RotOnly(Sub(Pos(w), pR), R);
                        TurnAboutSelf(w, R);
                        w.m[12] = pR.x + q.x; w.m[13] = pR.y + q.y; w.m[14] = pR.z + q.z;
                    };
                    about(staff);
                    Mat rh = newW[rk];
                    about(rh);
                    Reach(h, 0, Pos(rh), turn, c, s, baseW, newW);
                    rh.m[12] = newW[rk].m[12]; rh.m[13] = newW[rk].m[13]; rh.m[14] = newW[rk].m[14];
                    newW[rk] = rh;
                    std::vector<char> fromRight(n, 0);
                    fromRight[rk] = 1;
                    Propagate(h, oldW, newW, fromRight, rk);   // the fingers
                    axis = Norm(RotOnly(shaftInStaff, staff));
                    origin = Pos(staff);
                }
            }
            // Along the shaft as far as the controller's grip point is, within
            // reach of the staff's length either way, and never inside the right
            // fist. No left controller: where the game's grip has the hand.
            float along = gap;
            if (in.leftStaffAtM >= 0.0f) {
                // A crossbow: the left hand holds it at one place - under the stock just
                // behind the bow - it does not slide ("only in one place", 2026-10-05).
                const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
                along = in.leftStaffAtM * std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;
            } else if (in.leftHilt) {
                // A two-handed sword: the left fist next to the right one on the hilt, a little
                // play either way with the controller, never past it.
                const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
                const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
                along = in.leftHiltAtM * upm;
                if (in.handsValid & (1u << sp))
                    along = std::clamp(Dot(Sub(ControllerPoint(h, sp, in, newW, c, s), origin), axis) - tR,
                                       (in.leftHiltAtM - in.leftHiltSlideM) * upm, (in.leftHiltAtM + in.leftHiltSlideM) * upm);
            } else if (in.handsValid & (1u << sp)) {
                along = std::clamp(Dot(Sub(ControllerPoint(h, sp, in, newW, c, s), origin), axis) - tR, -kStaffReach * gap, kStaffReach * gap);
                if (fabsf(along) < 0.3f * gap) along = along < 0.0f ? -0.3f * gap : 0.3f * gap;
            }
            const V3 onShaft = onBarrel ? barrelAt : Add(origin, Scale(axis, tR + along));
        if (gun) {   // [hands] xbow_handover: the support hand's place, for the next pose's "nearer the grip than the fore-grip"
            g_xbowSupportAt = onShaft;
            g_xbowSupportKey = ((((uint64_t)HeroGen() << 32) | (uint32_t)in.weaponType)) ^ ((uint64_t)(holdSide & 1) << 63);
        }
            const V3 handAxis = onBarrel && haveBarrelDir ? barrelDir : axis;   // what the hand lies along (the barrel as drawn, or the shaft / the shot's line)
            // The hand on it: turned only so that the line through its fist lies
            // along the shaft; its roll about the shaft stays the controller's -
            // whole, the fist went round the shaft whenever the right hand
            // twisted the staff. Then moved so the fist's middle (the staff's
            // origin in the game's grip) sits on the shaft.
            const Mat inHand = InvAffine(grip.inStaff[sp]);   // staff frame -> the left hand's own
            V3 alongInHand = RotOnly(shaftInStaff, inHand), palmInHand = Through(inHand, {0.0f, 0.0f, 0.0f});
            const Mat freeHand = newW[lk];
            // [hands] xbow_wrist_tracked: a crossbow's left hand keeps the controller's turn (it may hold the stock from below or above -
            // turned into the game's grip round the line it went through a near half turn under it); its palm's middle (wrist to middle
            // knuckle, halfway) goes on the place.
            const bool trackedWrist = gun && in.xbowWrist && h.mid[sp] >= 0;
            // A two-handed sword hangs on the RIGHT wrist and the game's animation keeps the left
            // hand off it: the bone's origin is in the right fist, half a metre from the left one,
            // and the left hand stood that far off the hilt (2026-10-06). The left hand's own fist
            // instead: the hilt runs through it along the knuckles, index -> little finger (toward
            // the pommel, below the right fist), through the palm's middle (wrist to middle knuckle).
            // The same for every shaft laid by its bone's axis (spear, polearm, axe, mace): the game's
            // left hand is not on it either.
            if ((in.leftHilt || (in.shaftAxis != 0 && !gun)) && h.ind[sp] >= 0 && h.pnk[sp] >= 0 && h.mid[sp] >= 0) {
                const Mat toHand = InvAffine(freeHand);
                alongInHand = RotOnly(Sub(Pos(newW[h.pnk[sp]]), Pos(newW[h.ind[sp]])), toHand);
                palmInHand = PalmInHandOf(h, sp, newW);   // the palm's middle from the hand's own bones (the bind pose), not this frame's fingers
            }
            // the crossbow's support hand by its palm's middle too (test-57: "it holds by the wrist, not the palm's centre")
            if (trackedWrist) palmInHand = PalmInHandOf(h, sp, newW);
            // [hands] xbow_support_close: on the game's fore-grip the left hand takes the game's own support hand's turn (mirrored: that is its right
            // hand) - the palm the way the game's palm faces the stock, the knuckles along the stock as its knuckles - and later its fingers.
            const bool closeOn = gun && onFore && haveBarrelDir && in.xbowSupportClose && h.bindOk && h.mid[sp] >= 0 && h.ind[sp] >= 0 && h.pnk[sp] >= 0;
            if (closeOn) palmInHand = PalmInHandOf(h, sp, newW);
            Mat onStaff = freeHand;
            bool closeTurned = false;
            if (closeOn) {
                // the left hand's own out-of-the-palm and knuckle line, in its bone's frame, from its bind pose (toward the cross product on the left)
                const Mat toBind = InvAffine(h.bind[lk]);
                const V3 pt = Sub(Pos(h.bind[h.mid[sp]]), Pos(h.bind[lk])), ac = Sub(Pos(h.bind[h.ind[sp]]), Pos(h.bind[h.pnk[sp]]));
                const V3 outL = RotOnly(Scale(Cross(pt, ac), sp == 0 ? -1.0f : 1.0f), toBind), acL = RotOnly(ac, toBind);   // (away from it on the right hand)
                // the game's support palm and knuckles, from the crossbow's frame into the model, mirrored across its upright plane
                const xbowgrip::V side = xbowgrip::Cross({barrelDir.x, barrelDir.y, barrelDir.z}, {barrelUp.x, barrelUp.y, barrelUp.z});
                const V3 fW = RotOnly(fore.facing, staff), aW = RotOnly(fore.across, staff);
                const bool mirror = sp != foreOther;   // our support hand on the other side than the game's: its mirror image
                const xbowgrip::V outW = mirror ? xbowgrip::MirrorDir({fW.x, fW.y, fW.z}, side) : xbowgrip::V{fW.x, fW.y, fW.z};
                const xbowgrip::V acW = mirror ? xbowgrip::MirrorDir({aW.x, aW.y, aW.z}, side) : xbowgrip::V{aW.x, aW.y, aW.z};
                xbowgrip::M hm, turned;
                memcpy(hm.m, onStaff.m, sizeof hm.m);
                if (xbowgrip::HandTurnTo(hm, {outL.x, outL.y, outL.z}, {acL.x, acL.y, acL.z}, outW, acW, &turned)) {
                    memcpy(onStaff.m, turned.m, sizeof onStaff.m);
                    closeTurned = true;
                }
            }
            if (!trackedWrist && !closeTurned) TurnAboutSelf(onStaff, RotBetween(RotOnly(alongInHand, freeHand), handAxis));
            // [hands] xbow_left_roll: turned about the shaft on top (a crossbow's stock
            // is held from below, palm up - the controller's own roll left it on the side).
            if (const float deg = in.leftStaffRollDeg; deg != 0.0f) {
                const V3 k = handAxis;
                const float t = deg * 0.0174532925f, cs = cosf(t), sn = sinf(t), C = 1.0f - cs;
                Mat r{};   // row-vector form of the turn about k
                r.m[0] = cs + k.x * k.x * C;       r.m[1] = k.y * k.x * C + k.z * sn; r.m[2] = k.z * k.x * C - k.y * sn;
                r.m[4] = k.x * k.y * C - k.z * sn; r.m[5] = cs + k.y * k.y * C;       r.m[6] = k.z * k.y * C + k.x * sn;
                r.m[8] = k.x * k.z * C + k.y * sn; r.m[9] = k.y * k.z * C - k.x * sn; r.m[10] = cs + k.z * k.z * C;
                r.m[15] = 1.0f;
                TurnAboutSelf(onStaff, r);
            }
            const V3 palmNow = RotOnly(palmInHand, onStaff);
            onStaff.m[12] = onShaft.x - palmNow.x; onStaff.m[13] = onShaft.y - palmNow.y; onStaff.m[14] = onShaft.z - palmNow.z;
            // [hands] left_staff_x/y/z: the hand moved along its own axes (cm at
            // the hero's scale), set by eye until the fist sits round the shaft.
            // [hands] left_hold_pitch/yaw/roll: the hand turned about its own axes, round its point on the
            // shaft - the fist stays on it (Weapon Adjust, every two-handed weapon).
            if (const float* t = in.leftStaffTurnDeg; t[0] != 0.0f || t[1] != 0.0f || t[2] != 0.0f) {
                V3 ax[3];
                for (int r = 0; r < 3; ++r) ax[r] = Norm(V3{onStaff.m[r * 4], onStaff.m[r * 4 + 1], onStaff.m[r * 4 + 2]});
                const Mat R = Mul(Mul(TurnAbout(ax[0], t[0]), TurnAbout(ax[1], t[1])), TurnAbout(ax[2], t[2]));
                const V3 q = RotOnly(Sub(Pos(onStaff), onShaft), R);
                TurnAboutSelf(onStaff, R);
                onStaff.m[12] = onShaft.x + q.x; onStaff.m[13] = onShaft.y + q.y; onStaff.m[14] = onShaft.z + q.z;
            }
            if (const float* o = in.leftStaffOffset; o[0] != 0.0f || o[1] != 0.0f || o[2] != 0.0f) {
                const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
                const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
                for (int r = 0; r < 3; ++r) {
                    const V3 ax = Norm(V3{onStaff.m[r * 4], onStaff.m[r * 4 + 1], onStaff.m[r * 4 + 2]});
                    onStaff.m[12] += ax.x * o[r] * 0.01f * upm; onStaff.m[13] += ax.y * o[r] * 0.01f * upm; onStaff.m[14] += ax.z * o[r] * 0.01f * upm;
                }
            }
            if (e > 0.0f) {
                const V3 F = Pos(freeHand), O = Pos(onStaff);
                Reach(h, sp, Add(F, Scale(Sub(O, F), e)), turn, c, s, baseW, newW);
                Mat m = BlendRot(freeHand, onStaff, e);
                m.m[12] = newW[lk].m[12]; m.m[13] = newW[lk].m[13]; m.m[14] = newW[lk].m[14];
                newW[lk] = m;
                std::vector<char> fromHand(n, 0);
                fromHand[lk] = 1;
                Propagate(h, oldW, newW, fromHand, lk);   // the fingers
            }
            // [hands] xbow_support_close: the fingers closed on the stock as the game's support hand closes them (its pose carried onto the left
            // hand by grip_pose.h, the way the weapon weight's grip fingers are) - once the hand is on it
            if (closeOn && e >= 0.5f) {
                static int toldFingers = -1;
                const char* why = !foreFingers.ok || foreFingers.skel != h.skel ? "no pose read off the game's support hand yet" : nullptr;
                if (!why && sp == foreOther) {   // our support hand on the game's own side: its fingers as they were on that hand
                    size_t i = 0;
                    for (const Hero::Finger& f : h.fingers[sp])
                        for (const int b : f.bones) { if (i < foreRel.size()) newW[b] = Mul(foreRel[i], newW[lk]); ++i; }
                    if (i != foreRel.size()) why = "the hand's fingers changed";
                } else if (!why) {
                    std::vector<Mat> posed = newW;
                    why = GripPoseOnto(h, sp, foreFingers, newW, posed);
                    if (!why)
                        for (const Hero::Finger& f : h.fingers[sp])
                            for (const int b : f.bones) newW[b] = posed[b];
                }
                const int now = why ? 0 : 1;
                if (now != toldFingers) {
                    toldFingers = now;
                    XbowNote(why ? std::string("crossbow left hand: the fingers keep the game's grip of that hand - ") + why
                                 : std::string("crossbow left hand: closed on the stock - the palm turned and the fingers set as the game's own support hand holds the fore-grip (mirrored)"));
                }
            }
            // [debug] xbow_barrel_line: where the left palm's middle really is now (the pose's own wrist and middle knuckle) against its place
            if (gun && h.mid[sp] >= 0 && e > 0.99f) {   // which point the left hand is on, once per weapon and choice: the palm's middle, measured
                static int toldLeft = -1;
                const int key = (in.weaponType & 0x0FFFFFFF) * 8 + (onFore ? 4 : 0) + (onBarrel ? 2 : 0) + (trackedWrist ? 1 : 0);
                if (key != toldLeft) {
                    toldLeft = key;
                    const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
                    const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;
                    const V3 wrist = Pos(newW[lk]), palm = Scale(Add(wrist, Pos(newW[h.mid[sp]])), 0.5f);
                    // the arm's reach against where its wrist was sent (a place beyond it leaves the palm short of it)
                    const float armCm = (Len(Sub(Pos(newW[h.lwr[sp]]), Pos(newW[h.upr[sp]]))) + Len(Sub(Pos(newW[lk]), Pos(newW[h.lwr[sp]])))) / upm * 100.0f;
                    const float sentCm = Len(Sub(Pos(onStaff), Pos(newW[h.upr[sp]]))) / upm * 100.0f;
                    char t[360];
                    snprintf(t, sizeof t, "crossbow left hand: on %s, by its palm's middle (from the hand's bind pose) - the palm's middle %.1f cm from the place, the wrist %.1f cm; "
                             "the arm reaches %.0f cm, its wrist was sent %.0f cm from the shoulder%s%s",
                             onFore ? "the game's own fore-grip" : onBarrel ? "the place under the drawn barrel" : "the shot line", Len(Sub(palm, onShaft)) / upm * 100.0f,
                             Len(Sub(wrist, onShaft)) / upm * 100.0f, armCm, sentCm, sentCm > armCm ? " - OUT OF REACH" : "",
                             trackedWrist ? "" : " (the hand turned into the game's grip)");
                    XbowNote(t);
                }
            }
            if (gun && in.xbowBarrelDebug && h.mid[sp] >= 0) {
                const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
                const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
                auto back = [&](V3 q) {   // the barrel's frame: from the eye, head turn only, metres, less the hands' offset
                    const V3 d = Scale(Sub(q, head), 1.0f / upm);
                    const V3 m = {d.x * c - d.z * s, d.y, d.x * s + d.z * c};
                    return V3{-m.x - in.handOffset[0], m.y - in.handOffset[1], -m.z - in.handOffset[2]};
                };
                const V3 wrist = Pos(newW[lk]), palm = Scale(Add(wrist, Pos(newW[h.mid[sp]])), 0.5f);
                const float cm = Len(Sub(palm, onShaft)) / upm * 100.0f, wristCm = Len(Sub(wrist, onShaft)) / upm * 100.0f;
                const V3 P = back(palm), W = back(wrist), T = back(onShaft);
                const ULONGLONG now = GetTickCount64();
                const XbowPalmOut po{{P.x, P.y, P.z}, cm, now};
                AcquireSRWLockExclusive(&g_gunBoneLock); g_xbowPalm = po; ReleaseSRWLockExclusive(&g_gunBoneLock);
                static ULONGLONG toldPalmAt = 0;
                if (now - toldPalmAt >= 3000) {
                    toldPalmAt = now;
                    char t[360];
                    snprintf(t, sizeof t, "crossbow left hand (held %.2f, %s, %s): palm's middle %.1f cm from its place, the wrist %.1f cm - head frame cm: place %.1f %.1f %.1f, "
                             "palm %.1f %.1f %.1f, wrist %.1f %.1f %.1f", e, onBarrel ? "under the drawn barrel" : "on the shot line",
                             trackedWrist ? "the wrist as the controller" : "the game's grip turn", cm, wristCm, T.x * 100, T.y * 100, T.z * 100,
                             P.x * 100, P.y * 100, P.z * 100, W.x * 100, W.y * 100, W.z * 100);
                    XbowNote(t);
                }
            }
            // The staff where the hands put it: the left wrist re-derived it
            // (the game hangs it under that wrist).
            newW[wb] = staff;
            std::vector<char> fromStaff(n, 0);
            fromStaff[wb] = 1;
            Propagate(h, oldW, newW, fromStaff, wb);
            // For the aim: the shaft from the left hand's side toward the right
            // one, back in the head's turn-only frame (ToModel undone).
            const V3 d = Scale(aimBarrel ? aimDir : axis, -1.0f);   // [hands] xbow_aim_barrel: the drawn barrel, else the shaft / the shot line
            const V3 m = {d.x * c - d.z * s, d.y, d.x * s + d.z * c};
            AcquireSRWLockExclusive(&g_staffLock);
            g_staff.dir[0] = -m.x; g_staff.dir[1] = m.y; g_staff.dir[2] = -m.z;
            g_staff.gap = gap;
            g_staff.at = GetTickCount64();
            ReleaseSRWLockExclusive(&g_staffLock);
            // For [debug]: the stretch of the shaft the left hand may take, from the eye in the
            // head's turn-only frame, metres (a sword's hilt drawn at least 25 cm each way). A
            // crossbow's line is vrcam's HandRay.
            if (!gun) {
                const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
                const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
                float lo = tR - kStaffReach * gap, hi = tR + kStaffReach * gap;
                if (in.leftHilt) {
                    lo = tR + std::min(-0.25f, in.leftHiltAtM - in.leftHiltSlideM) * upm;
                    hi = tR + std::max(0.25f, in.leftHiltAtM + in.leftHiltSlideM) * upm;
                }
                auto back = [&](V3 p) {
                    const V3 d = Scale(Sub(p, head), 1.0f / upm);
                    const V3 m = {d.x * c - d.z * s, d.y, d.x * s + d.z * c};
                    return V3{-m.x, m.y, -m.z};
                };
                const V3 A = back(Add(origin, Scale(axis, lo))), B = back(Add(origin, Scale(axis, hi)));
                GrabLineOut g{{A.x, A.y, A.z}, {B.x, B.y, B.z}, GetTickCount64()};
                AcquireSRWLockExclusive(&g_gunBoneLock); g_grabLine = g; ReleaseSRWLockExclusive(&g_gunBoneLock);
            }
        }
    // [hands] xbow_grip_latch: a crossbow waiting for its grip is not put into both hands this way (it has no letting go)
    } else if (in.staff && in.mode == 2 && (in.handsValid & 3u) == 3u && !(in.gunStock && in.xbowLatch)) {
        const int hold = in.staffHand == 0 ? 0 : 1, wb = h.weapon[hold], other = h.hand[hold ^ 1];
        if (wb >= 0 && other >= 0) {
            auto apply = [](const Mat& m, V3 v) {   // a point through a row-vector matrix
                return V3{v.x * m.m[0] + v.y * m.m[4] + v.z * m.m[8] + m.m[12],
                          v.x * m.m[1] + v.y * m.m[5] + v.z * m.m[9] + m.m[13],
                          v.x * m.m[2] + v.y * m.m[6] + v.z * m.m[10] + m.m[14]};
            };
            // the other hand, in the staff's frame: from the grip once taken, else this frame's animation
            const V3 onStaff = grip.ok ? Pos(grip.inStaff[hold ^ 1]) : apply(InvAffine(oldW[wb]), Pos(oldW[other]));
            const V3 gripAt = Pos(newW[wb]);
            V3 want = Pos(newW[other]);
            // [hands] staff_x/y/z: the point it runs through, moved along the
            // other hand's own axes (cm at the hero's scale), set by eye.
            const float* o = in.staffOffset;
            if (o[0] != 0.0f || o[1] != 0.0f || o[2] != 0.0f) {
                const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head >= 0 ? h.head : other]).y;
                const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
                const Mat& m = newW[other];
                for (int r = 0; r < 3; ++r) {
                    const V3 axis = Norm(V3{m.m[r * 4], m.m[r * 4 + 1], m.m[r * 4 + 2]});
                    want = Add(want, Scale(axis, o[r] * 0.01f * upm));
                }
            }
            const V3 now = apply(newW[wb], onStaff);
            if (Len(Sub(now, gripAt)) > 0.05f && Len(Sub(want, gripAt)) > 0.05f) {
                TurnAboutSelf(newW[wb], RotBetween(Sub(now, gripAt), Sub(want, gripAt)));
                std::vector<char> fromWeapon(n, 0);
                fromWeapon[wb] = 1;
                Propagate(h, oldW, newW, fromWeapon, wb);
            }
            // [hands] follow_staff: the wrists turn with the staff, the way the
            // game's animation grips it, whatever the controllers' own turn -
            // a fist round a staff does not twist away from it. Each hand keeps
            // its place; its turn is the animation's, relative to the staff.
            if (in.staffHands) {
                const Mat staff = newW[wb];
                for (const int side : {hold, hold ^ 1}) {
                    const int k = h.hand[side];
                    Mat m = Mul(grip.ok ? grip.inStaff[side] : Mul(oldW[k], InvAffine(oldW[wb])), staff);
                    m.m[12] = newW[k].m[12]; m.m[13] = newW[k].m[13]; m.m[14] = newW[k].m[14];
                    newW[k] = m;
                    std::vector<char> fromHand(n, 0);
                    fromHand[k] = 1;
                    Propagate(h, oldW, newW, fromHand, k);   // the fingers
                }
                // the staff stays where it was put (through both hands, Weapon Adjust)
                newW[wb] = staff;
                std::vector<char> fromStaff(n, 0);
                fromStaff[wb] = 1;
                Propagate(h, oldW, newW, fromStaff, wb);
            }
        }
    }

    // The forearms as they will be drawn, for FlatVR (g_forearm): the same
    // head point and scale the hands were placed with, undone.
    if (h.head >= 0) {
        const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
        const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;
        for (int side = 0; side < 2; ++side) {
            const int l = h.lwr[side], k = h.hand[side];
            if (l < 0 || k < 0 || upm <= 0.0f) continue;
            const V3 E = Pos(newW[l]), W = Pos(newW[k]);
            const V3 axis = Norm(Sub(W, E));
            const V3 across = PalmAcross(h, side, newW, axis);
            ForearmOut f{};
            FromModel(V3{(E.x - head.x) / upm, (E.y - head.y) / upm, (E.z - head.z) / upm}, c, s, f.elbow);
            FromModel(V3{(W.x - head.x) / upm, (W.y - head.y) / upm, (W.z - head.z) / upm}, c, s, f.wrist);
            FromModel(across, c, s, f.across);
            V3 pointU{0, 0, 0}, faceU{0, 0, 0};   // the hand's own frame, when the knuckles are found (model space)
            if (const int mk = h.mid[side], ik = h.ind[side], pk = h.pnk[side]; mk >= 0 && ik >= 0 && pk >= 0) {
                const V3 M = Pos(newW[mk]), point = Sub(M, W), line = Sub(Pos(newW[ik]), Pos(newW[pk]));
                const V3 face = Norm(Scale(Cross(point, line), side == 0 ? -1.0f : 1.0f));   // out of the palm, as Fist has it
                pointU = Norm(point);
                faceU = face;
                f.handW = true;
                const float out = 0.03f * upm;
                const V3 P{W.x + point.x * 0.5f + face.x * out, W.y + point.y * 0.5f + face.y * out, W.z + point.z * 0.5f + face.z * out};
                FromModel(V3{(P.x - head.x) / upm, (P.y - head.y) / upm, (P.z - head.z) / upm}, c, s, f.palm);
                f.palmModel[0] = P.x; f.palmModel[1] = P.y; f.palmModel[2] = P.z;
                for (int i = 0; i < 3; ++i) f.palm[i] -= in.handOffset[i];
                f.palmOk = true;
                // [hands] spell_aim_model: this hand as drawn, for the aim (CastPoseNow) - the palm's middle (halfway to the middle knuckle), the
                // fingers and the way out of the palm, from the eye in the head's turn-only frame, as GunBone (the hands' offset left in: where it is drawn)
                CastHandOut ch{};
                const V3 mid{W.x + point.x * 0.5f, W.y + point.y * 0.5f, W.z + point.z * 0.5f};
                FromModel(V3{(mid.x - head.x) / upm, (mid.y - head.y) / upm, (mid.z - head.z) / upm}, c, s, ch.palm);
                FromModel(pointU, c, s, ch.fingers);
                FromModel(faceU, c, s, ch.face);
                ch.at = GetTickCount64();
                AcquireSRWLockExclusive(&g_castLock); g_castHand[side] = ch; ReleaseSRWLockExclusive(&g_castLock);
            }
            for (int i = 0; i < 3; ++i) { f.elbow[i] -= in.handOffset[i]; f.wrist[i] -= in.handOffset[i]; }
            f.at = GetTickCount64();
            {   // The same arm in the WORLD, the hero's own model-to-world (his TransformComponent's local matrix, read now, before this very pose is used - as
                // NotePosedWeapon does: a later copy displaced fast swings) through the bones as posed: exactly where the arm is drawn, and from the very same
                // instant, so the wrist orb rides the drawn hand as a belt bottle in the palm does (vrcam's wrist balls; HeroArmWorld). World units; directions unit.
                // The matrix is not always readable (the entity tables are looked for at most twice a second after a miss, a loading hero is in none): then this pose has no world
                // arm, and the LAST one is kept with its own time (worldAt), so the reader can see how old it is and carry it on with the controller instead of dropping the ball.
                HeroXform hx;
                Mat model;
                const char* why = "";
                if (!HeroTransform(&hx, &why)) {
                    if (!why || !*why) why = "the hero's transform could not be looked up";
                } else if (hx.self != self) why = "the transform found is not this skeleton's";
                else if ((hx.parent & 0xFFFFFu) != 0xFFFFFu) why = "the hero's transform has a parent (not the world's)";
                else if (!SafeCopy(model.m, (const void*)hx.comp, sizeof model)) why = "the hero's transform could not be read";
                else {
                    PoseModel(model);   // [body] under_camera: as the arm will be drawn
                    const V3 ew = Through(model, E), ww = Through(model, W);
                    const V3 xw = Norm(RotOnly(across, model)), pw = Norm(RotOnly(pointU, model)), fw = Norm(RotOnly(faceU, model));
                    if (std::isfinite(ew.x + ew.y + ew.z + ww.x + ww.y + ww.z + xw.x + xw.y + xw.z + pw.x + pw.y + pw.z + fw.x + fw.y + fw.z)) {
                        f.elbowW[0] = ew.x; f.elbowW[1] = ew.y; f.elbowW[2] = ew.z;
                        f.wristW[0] = ww.x; f.wristW[1] = ww.y; f.wristW[2] = ww.z;
                        f.acrossW[0] = xw.x; f.acrossW[1] = xw.y; f.acrossW[2] = xw.z;
                        f.pointW[0] = pw.x; f.pointW[1] = pw.y; f.pointW[2] = pw.z;
                        f.faceW[0] = fw.x; f.faceW[1] = fw.y; f.faceW[2] = fw.z;
                        f.worldOk = true;
                        f.worldAt = f.at;
                        why = "";
                    } else why = "the arm in the world was not a number";
                }
                g_armWorldWhy.store(why);
            }
            AcquireSRWLockExclusive(&g_forearmLock);
            if (!f.worldOk && g_forearm[side].worldOk) {   // keep the last world arm (and its time), the rest of this pose is new
                const ForearmOut& was = g_forearm[side];
                memcpy(f.elbowW, was.elbowW, sizeof f.elbowW); memcpy(f.wristW, was.wristW, sizeof f.wristW); memcpy(f.acrossW, was.acrossW, sizeof f.acrossW);
                memcpy(f.pointW, was.pointW, sizeof f.pointW); memcpy(f.faceW, was.faceW, sizeof f.faceW);
                f.handW = was.handW; f.worldOk = true; f.worldAt = was.worldAt;
            }
            g_forearm[side] = f;
            ReleaseSRWLockExclusive(&g_forearmLock);
        }
    }

    if (in.mode == 2 && in.holdGrip && grip.ok) {
        const int hs = freeStaff ? staffSide : in.weaponSide ? 1 : 0;
        if (h.weapon[hs] >= 0 && h.hand[hs] >= 0 && h.head >= 0) {
            const float headY = in.haveEye ? in.eyeModel[1] : Pos(oldW[h.head]).y;
            const float cm = 100.0f / (std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale);
            const Mat now = Mul(oldW[h.weapon[hs]], InvAffine(oldW[h.hand[hs]]));
            const V3 a = Norm(V3{now.m[4], now.m[5], now.m[6]}), b = Norm(V3{grip.attach[hs].m[4], grip.attach[hs].m[5], grip.attach[hs].m[6]});
            g_weaponCalm.store(Len(Sub(Pos(now), Pos(grip.attach[hs]))) * cm < 2.0f && Dot(a, b) > 0.9976f);   // cos 4 deg
        }
    }
    if (in.bothAttach && grip.ok && h.bindOk && h.hand[0] >= 0 && h.hand[1] >= 0 && h.weapon[0] >= 0 && h.weapon[1] >= 0) {
        const float headY = in.haveEye ? in.eyeModel[1] : Pos(oldW[h.head >= 0 ? h.head : 0]).y;
        const float cm = 100.0f / (std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale);   // model units -> cm
        for (int side = 0; side < 2; ++side) {
            const Mat now = Mul(oldW[h.weapon[side]], InvAffine(oldW[h.hand[side]]));
            const V3 d = Sub(Pos(now), Pos(grip.attach[side]));
            const V3 a = Norm(V3{now.m[4], now.m[5], now.m[6]}), b = Norm(V3{grip.attach[side].m[4], grip.attach[side].m[5], grip.attach[side].m[6]});
            g_diag[side * 2].store(Len(d) * cm);
            g_diag[side * 2 + 1].store(acosf(std::clamp(Dot(a, b), -1.0f, 1.0f)) * 57.2958f);
        }
        const Mat sR = Mul(InvAffine(h.bind[h.weapon[0]]), oldW[h.weapon[0]]), sL = Mul(InvAffine(h.bind[h.weapon[1]]), oldW[h.weapon[1]]);
        g_diag[4].store(Len(Sub(Pos(sR), Pos(sL))) * cm);
        g_diag[5].store(Len(Sub(Pos(oldW[h.weapon[0]]), Pos(oldW[h.weapon[1]]))) * cm);
        g_diag[6].store(Len(Sub(Pos(oldW[h.weapon[0]]), Pos(oldW[h.hand[0]]))) * cm);
        g_diag[7].store(Len(Sub(Pos(oldW[h.weapon[1]]), Pos(oldW[h.hand[0]]))) * cm);
    }
    // The body's two-handed weapon bone where the holding wrist's attach bone is: walking, the game
    // draws the great axe from it, and it went where the animation carries it - along the arm.
    // Same world matrix (an item model follows its attach bone as it is); g_diag[8..9] say how far
    // the game's own pose has it from the wrist's attach, standing and walking.
    if (in.bothAttach && in.mode == 2 && in.holdGrip && grip.ok && h.twoHand >= 0) {
        const int hs = freeStaff ? staffSide : in.weaponSide ? 1 : 0, from = h.weapon[hs];
        if (from >= 0) {
            const float headY = in.haveEye ? in.eyeModel[1] : Pos(oldW[h.head >= 0 ? h.head : 0]).y;
            const float cm = 100.0f / (std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale);
            const V3 a = Norm(V3{oldW[h.twoHand].m[4], oldW[h.twoHand].m[5], oldW[h.twoHand].m[6]}), b = Norm(V3{oldW[from].m[4], oldW[from].m[5], oldW[from].m[6]});
            g_diag[8].store(Len(Sub(Pos(oldW[h.twoHand]), Pos(oldW[from]))) * cm);
            g_diag[9].store(acosf(std::clamp(Dot(a, b), -1.0f, 1.0f)) * 57.2958f);
            // (not moved any more: [debug] shrink_bone showed the weapon never hangs on it, 2026-10-06)
        }
    }
    // bothAttach: the other wrist's attach bone where the holding one is - whichever of the two
    // the game hangs the weapon on in this animation, it stays in the holding hand.
    if (in.bothAttach && in.mode == 2 && in.holdGrip && grip.ok) {
        const int hs = freeStaff ? staffSide : in.weaponSide ? 1 : 0, from = h.weapon[hs], to = h.weapon[hs ^ 1];
        if (from >= 0 && to >= 0 && h.bindOk && (in.handsValid & (1u << (freeStaff ? 0 : hs)))) {
            // Running, the game puts a two-handed weapon in the LEFT hand (it said so, 2026-10-06: "standing
            // it is in the hand, running he takes it with the left"); the item follows L_weapon_attach then.
            // The same world matrix on it made the axe vanish, bind(to) bind(from)^-1 turned it 72 deg: the
            // left attach bone is a mirror of the right one (negative determinant) - the item drawn from
            // a mirror-less matrix is inside out and culled. So the right one's matrix, with its own X
            // axis turned over whenever the game's left bone is mirrored and the right one is not.
            if (!weightDrew) newW[to] = newW[from];   // (a weapon the weight model draws has both bones set in its block, the other one apart from the slot's)
            // ...and moving, the game lays a two-handed weapon along the line from the right attach bone
            // to the left one (both hands on the haft): with the two in one point the line had no length
            // and the axe was squashed flat along its Y (10:23 log: its matrix had Y 0, both bones Y 1).
            // So the left one goes along the right one's own Y (+-in.carryAxis), 40 cm out.
            // (A weapon the weight model draws has this done in its own block - attachFor: the other bone carried the same way along the weapon's axis, with the mirror fix. It
            // had been left out there (2c95c81: "the item stood 40 cm off the shaft"), both bones in one point - and the staff came out flat, a disc: the line between them had no
            // length. That commit's reason - the item the game hangs on the left bone walking would stand 40 cm off - came from reading the code, "nothing seen in a headset";
            // the flat staff was. The log line "the game's own attach bones" tells what the two look like, for judging the gap.)
            if (!weightDrew) {
                const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head >= 0 ? h.head : 0]).y;
                const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;
                const int ax = std::clamp(abs(in.carryAxis), 1, 3) - 1;
                const float sg = in.carryAxis < 0 ? -1.0f : 1.0f;
                const V3 dir = Norm(V3{newW[from].m[ax * 4], newW[from].m[ax * 4 + 1], newW[from].m[ax * 4 + 2]});
                const float k = sg * in.carryM * upm;
                newW[to].m[12] += dir.x * k; newW[to].m[13] += dir.y * k; newW[to].m[14] += dir.z * k;
            }
            auto det = [](const Mat& m) {
                return Dot(V3{m.m[0], m.m[1], m.m[2]}, Cross(V3{m.m[4], m.m[5], m.m[6]}, V3{m.m[8], m.m[9], m.m[10]}));
            };
            const bool flip = !weightDrew && (det(oldW[to]) < 0.0f) != (det(newW[from]) < 0.0f);   // (the weight block's attachFor has turned it already)
            if (flip) for (int c2 = 0; c2 < 3; ++c2) newW[to].m[c2] = -newW[to].m[c2];
            g_diag[12].store(det(oldW[to])); g_diag[13].store(det(oldW[from]));
            std::vector<char> fromWeapon(n, 0);
            fromWeapon[to] = 1;
            Propagate(h, oldW, newW, fromWeapon, to);
        }
    }

    // The bone of the weapon in hand as it ended up, for vrcam's debug drawing ([debug]
    // bone_axes, phantom_ray): held alone in the right hand (staff, crossbow, spear, sword),
    // the bone it hangs on; else the hand slot it is in, or the other wrist when the game
    // shrank that one's bone (holds nothing there). Model space -> the head's turn-only
    // frame (ToModel undone), from the eye, metres.
    int axesSide = freeStaff ? staffSide : in.weaponSide ? 1 : 0;
    if (!freeStaff && h.weapon[0] >= 0 && h.weapon[1] >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
        auto size = [&](int side) { const Mat a = Mul(oldW[h.weapon[side]], InvAffine(oldW[h.hand[side]])); return Len({a.m[0], a.m[1], a.m[2]}); };
        if (size(axesSide) < 0.3f && size(axesSide ^ 1) > 0.7f) axesSide ^= 1;
    }
    if (h.weapon[axesSide] >= 0) {
        const Mat& w = newW[h.weapon[axesSide]];
        auto back = [&](V3 d) { const V3 m = {d.x * c - d.z * s, d.y, d.x * s + d.z * c}; return V3{-m.x, m.y, -m.z}; };
        const V3 head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head >= 0 ? h.head : 0]);
        const float upm = std::clamp(head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
        GunBoneOut g{};
        const V3 o = back(Scale(Sub(Pos(w), head), 1.0f / upm));
        g.o[0] = o.x; g.o[1] = o.y; g.o[2] = o.z;
        for (int i = 0; i < 3; ++i) {
            const V3 d = back(Norm(V3{w.m[i * 4], w.m[i * 4 + 1], w.m[i * 4 + 2]}));
            g.ax[i][0] = d.x; g.ax[i][1] = d.y; g.ax[i][2] = d.z;
        }
        g.at = GetTickCount64();
        AcquireSRWLockExclusive(&g_gunBoneLock); g_gunBone = g; ReleaseSRWLockExclusive(&g_gunBoneLock);
        // ([multiplayer] pose_share's attach bones as drawn are read at the end of the pose: DrawnAttach, DrawnHands.)
        {   // [hands] spell_aim_model: the same bone for the aim of a staff, wand or orb - its origin and +Y, and the wrist it is on
            CastWeaponOut cw{};
            cw.side = axesSide;
            memcpy(cw.origin, g.o, sizeof cw.origin);
            memcpy(cw.tip, g.ax[1], sizeof cw.tip);
            cw.at = Len(V3{w.m[4], w.m[5], w.m[6]}) > 0.001f && std::isfinite(o.x + o.y + o.z) ? g.at : 0;
            AcquireSRWLockExclusive(&g_castLock); g_castWeapon = cw; ReleaseSRWLockExclusive(&g_castLock);
        }
        // ([weight] the weight model's shaft is measured in the weight block above, from W - the bone this one is for a weapon it holds, either wrist.)
        if (in.posedWeapon) {
            // The weapon bones as drawn, for melee by touch's blades (vr/blade_shape.h): the same numbers as g above for this hand, and,
            // dual-wielding (a Barbarian's two blades, an Assassin's two claws), the other attach bone's - when the game has not shrunk it
            // (the bone of a hand that holds nothing is made a point).
            PosedBlade blades[2];
            auto keep = [&](int slot, const Mat& bone) {
                PosedBlade& b = blades[slot];
                const V3 bo = back(Scale(Sub(Pos(bone), head), 1.0f / upm));
                b.origin[0] = bo.x; b.origin[1] = bo.y; b.origin[2] = bo.z;
                for (int i = 0; i < 3; ++i) {
                    const V3 d = back(Norm(V3{bone.m[i * 4], bone.m[i * 4 + 1], bone.m[i * 4 + 2]}));
                    b.axis[i][0] = d.x; b.axis[i][1] = d.y; b.axis[i][2] = d.z;
                }
                b.ok = std::isfinite(bo.x + bo.y + bo.z) && Len(V3{bone.m[4], bone.m[5], bone.m[6]}) > 0.001f;
                b.unitM = weaponextent::UnitM(Len(V3{bone.m[4], bone.m[5], bone.m[6]}), upm);   // the weapon model's unit in metres as drawn (vr/weapon_extents.h)
                // a fist weapon's blade runs along the bone's Z: the sign of that axis against the forearm in the game's own pose (elbow -> wrist), told for the claws and katars
                if (h.weapon[slot] >= 0 && h.hand[slot] >= 0 && h.lwr[slot] >= 0) {
                    const Mat& gw = oldW[h.weapon[slot]];
                    const V3 zOld = {gw.m[8], gw.m[9], gw.m[10]}, fore = Sub(Pos(oldW[h.hand[slot]]), Pos(oldW[h.lwr[slot]]));
                    const float lz = Len(zOld), lf = Len(fore);
                    if (lz > 0.001f && lf > 0.001f) {
                        const float d = Dot(zOld, fore) / (lz * lf);
                        if (std::isfinite(d)) b.zSign = d > 0.25f ? 1 : d < -0.25f ? -1 : 0;
                    }
                }
                // [flail] chain: a flail's ball swinging on its chain is what strikes - its centre, the last link's way and its radius, carried by
                // this pose's attach bone (the ball's place on the item is the last chain pose's)
                if (b.ok && g_flailOn.load(std::memory_order_relaxed)) {
                    AcquireSRWLockShared(&g_flailBallLock); const FlailBall fb = g_flailBall; ReleaseSRWLockShared(&g_flailBallLock);
                    if (fb.at && GetTickCount64() - fb.at < 150 && fb.gen == HeroGen() && fb.side == slot) {
                        const Mat toModel = Mul(fb.itemLocal, bone);
                        const V3 cm = Add(RotOnly(fb.centre, toModel), Pos(toModel));
                        const V3 c2 = back(Scale(Sub(cm, head), 1.0f / upm)), d2 = back(Norm(RotOnly(fb.dir, toModel)));
                        const float r = fb.radius * Len(V3{toModel.m[0], toModel.m[1], toModel.m[2]}) / upm;
                        if (std::isfinite(c2.x + c2.y + c2.z + d2.x + d2.y + d2.z + r) && r > 0.0f && Len(d2) > 0.5f) {
                            b.ball = true;
                            b.ballCentre[0] = c2.x; b.ballCentre[1] = c2.y; b.ballCentre[2] = c2.z;
                            b.ballDir[0] = d2.x; b.ballDir[1] = d2.y; b.ballDir[2] = d2.z;
                            b.ballRadius = r;
                            b.moreBalls = 0;
                            for (int k = 0; k < fb.more && k < PosedBlade::kMoreBalls; ++k) {   // Khalim's other balls, carried the same way
                                const V3 mc = Add(RotOnly(fb.moreCentre[k], toModel), Pos(toModel));
                                const V3 m2 = back(Scale(Sub(mc, head), 1.0f / upm)), md = back(Norm(RotOnly(fb.moreDir[k], toModel)));
                                if (!std::isfinite(m2.x + m2.y + m2.z + md.x + md.y + md.z) || !(Len(md) > 0.5f)) continue;
                                float* mcOut = b.moreCentre[b.moreBalls];
                                float* mdOut = b.moreDir[b.moreBalls];
                                mcOut[0] = m2.x; mcOut[1] = m2.y; mcOut[2] = m2.z;
                                mdOut[0] = md.x; mdOut[1] = md.y; mdOut[2] = md.z;
                                ++b.moreBalls;
                            }
                        }
                    }
                }
            };
            keep(axesSide, w);
            const int other = axesSide ^ 1;
            if (in.dualBlades && h.weapon[other] >= 0 && h.hand[other] >= 0) {
                const Mat a = Mul(oldW[h.weapon[other]], InvAffine(oldW[h.hand[other]]));
                if (Len({a.m[0], a.m[1], a.m[2]}) > 0.5f) keep(other, newW[h.weapon[other]]);
            }
            NotePosedWeapon(self, w, axesSide, in, blades);
        }
    }

    // [hands] fist: free hands close and open with the controllers' grips.
    if (h.cls >= 0) g_heroClass.store(h.cls);
    // [weight]: a hand on the weapon that is not the slot's closes round it, a full fist; the slot's hand
    // off it is free (the game's grip stays only in the hand the game hangs it on).
    // [weight] grip_fit: the hand that took the weapon closes round it with the controller's squeeze (vr/grip_fit.h Close) even with [hands] fist off - it is on the
    // shaft by the weapon's own rule - and fully closed no longer: the fingers tighten with the grip.
    bool leftFisted = false, rightFisted = false;   // the free-hand rule closed that hand in this pose
    // [weight] grip_fit_fingers (vr/grip_pose.h): the hand that took the weapon has the game's own weapon-grip pose of the fingers and the thumb - the pose of the hand the game holds it
    // in, read above, put on this hand in its own frame - instead of closing with the squeeze: fixed while it holds the weapon, easing as it takes it and lets go. Until it is
    // fully on (poseE < 1) the fist rule below still draws the hand it comes from (or goes to); fully on, the fist rule leaves it alone. A pose that cannot be put on (the rig's two
    // hands do not answer each other, no pose read yet for this weapon): the fingers close with the squeeze as before.
    float poseE[2] = {0.0f, 0.0f};
    std::vector<Mat> poseW;   // the pose as it would stand: a copy of newW with the taking hand's finger bones set
    // [weight] grip_model 1: a hand the grip library laid has the library's fingers - the rotations it answered with, laid on the bones in the hand's frame as it stands now (the generic fist
    // rule below still draws the hand until the library's fingers begin, as for the game's pose)
    bool libFingers[2] = {false, false};
    if (wo.on && in.mode == 2 && h.bindOk)
        for (int side = 0; side < 2; ++side) {
            const LibGripResult& L = libGripRes[side];
            const float t = std::clamp(wo.on01[side], 0.0f, 1.0f);
            if (!L.ok || !L.out.fingersOn || !(t > 0.0f) || (side == 1 && leftOnStaff)) continue;
            grippose::M3 e;
            std::vector<grippose::M3> rows;
            if (!HandFrameAt(h, side, newW, &e) || !griprig::Compose(L.rig, L.mirror, griprig::ProperFrame(e, side), L.out.finger, &rows)) continue;
            std::vector<Mat> laid = poseW.empty() ? newW : poseW;
            size_t idx = 0;
            bool ok = true;
            for (const Hero::Finger& f : h.fingers[side])
                for (const int b : f.bones) {
                    const int pa = h.parent[b];
                    if (pa < 0 || idx >= rows.size()) { ok = false; break; }
                    Mat nw = Mul(Mul(h.bind[b], InvAffine(h.bind[pa])), laid[pa]);
                    for (int r = 0; r < 3; ++r) {
                        const float len = (float)boneguard::RowLen(newW[b].m, r);
                        for (int c2 = 0; c2 < 3; ++c2) nw.m[r * 4 + c2] = len * rows[idx].m[r * 3 + c2];
                        nw.m[r * 4 + 3] = 0.0f;
                    }
                    nw.m[15] = 1.0f;
                    for (float v : nw.m)
                        if (!std::isfinite(v)) ok = false;
                    laid[b] = nw;
                    ++idx;
                }
            if (!ok) continue;
            poseW = std::move(laid);
            poseE[side] = t * t * (3.0f - 2.0f * t);
            libFingers[side] = true;
        }
    // [weight] grab_wrap: a hand that holds a body ([weight] grab_bodies, grab_wire.cpp moved it onto the point the grab holds) is laid ON the body - turned so the palm faces
    // it, the palm's surface against the body's, and its fingers closed round it until each touches: the grip library's hand step (its port of the VR toolkit's dynamic
    // pose: the hand carried onto the body, the fingers easing closed) with the held limb's capsule for the shaft. No grip library: the palm is laid on the same way by the
    // frame below (its depth the hand's own, a fifth of the knuckles' span), eased on, and the fist closes with the grip as ever. Let go, the hand and its fingers ease back
    // to the controller's at the toolkit's pose blend (16 a second, exponential). Only the hand moves; the arm is reached to it again and the fingers follow.
    {
        struct GrabEase {
            bool holding = false, have = false;
            gripfit::Rot r;
            V3 shift{};
            std::vector<Mat> fingerRel;   // the fingers held, against the hand (none: the fist's own)
            double heldSince = 0.0, releasedAt = 0.0;
        };
        static GrabEase ease[2];   // (the hook's thread)
        constexpr float kPoseBlend = 16.0f;   // the toolkit's hand pose blend, 1/s (HVRHandAnimator)
        LARGE_INTEGER qc, qf;
        QueryPerformanceCounter(&qc);
        QueryPerformanceFrequency(&qf);
        const double nowS = (double)qc.QuadPart / (double)qf.QuadPart;
        // [weight]: the weapon's bones (its attach bones and what hangs on them) are the weight block's - slung on the back, in the other hand: the hand laid here does not
        // carry them (the hand's turn reached them through its subtree: the headset 0.183, the spear on the back swung round into the holding hand, «опять к руке
        // примагничивается оружие»)
        std::vector<char> weaponBone;
        if (weightDrew) {
            weaponBone.assign(newW.size(), 0);
            for (int sd = 0; sd < 2; ++sd)
                if (h.weapon[sd] >= 0 && h.weapon[sd] < (int)weaponBone.size()) weaponBone[h.weapon[sd]] = 1;
            for (int i = 0; i < h.count && i < (int)weaponBone.size(); ++i)
                if (h.parent[i] >= 0 && h.parent[i] < (int)weaponBone.size() && weaponBone[h.parent[i]]) weaponBone[i] = 1;
        }
        for (int side = 0; side < 2 && in.mode == 2 && h.bindOk && h.head >= 0; ++side) {
            const int k = h.hand[side], mk = h.mid[side], ik = h.ind[side], pk = h.pnk[side];
            const bool bonesOk = k >= 0 && mk >= 0 && ik >= 0 && pk >= 0, tracked = (in.handsValid & (1u << side)) != 0;
            const bool holding = in.grabWrap && in.grabHolding[side] && bonesOk && tracked;
            GrabEase& E = ease[side];
            if (holding && !E.holding) E.heldSince = nowS;
            if (!holding && E.holding) E.releasedAt = nowS;
            E.holding = holding;
            if (!bonesOk) continue;
            const V3 wristP = Pos(newW[k]), midP = Pos(newW[mk]);
            const V3 palm = Scale(Add(wristP, midP), 0.5f);   // the palm's middle, as the weapon grip has it
            // the hand turned by r about the palm's middle and moved by shift, e of the way; the arm reached to it again, the fingers with it
            const auto applyFit = [&](const gripfit::Rot& r, V3 shift, float e) {
                std::vector<std::pair<int, Mat>> keepWeapon;   // (the weight block's weapon bones, put back after the arm and the hand are moved)
                for (int i = 0; i < (int)weaponBone.size(); ++i)
                    if (weaponBone[i]) keepWeapon.emplace_back(i, newW[i]);
                struct PutBack {
                    std::vector<std::pair<int, Mat>>& keep;
                    std::vector<Mat>& w;
                    ~PutBack() { for (const auto& kv : keep) w[kv.first] = kv.second; }
                } putBack{keepWeapon, newW};
                const V3 pm = Add(palm, shift);
                const gripfit::V3 w2 = gripfit::Add({pm.x, pm.y, pm.z}, gripfit::Apply(gripfit::Sub({wristP.x, wristP.y, wristP.z}, {palm.x, palm.y, palm.z}), r));
                const Mat before = newW[k];
                Mat target = before;
                for (int i = 0; i < 3; ++i) {
                    const gripfit::V3 rw = gripfit::Apply({before.m[i * 4], before.m[i * 4 + 1], before.m[i * 4 + 2]}, r);
                    target.m[i * 4] = rw.x; target.m[i * 4 + 1] = rw.y; target.m[i * 4 + 2] = rw.z;
                }
                Reach(h, side, Add(wristP, Scale(Sub(V3{w2.x, w2.y, w2.z}, wristP), e)), turn, c, s, baseW, newW);
                Mat fitted = e >= 1.0f ? target : BlendRot(before, target, e);
                fitted.m[12] = newW[k].m[12]; fitted.m[13] = newW[k].m[13]; fitted.m[14] = newW[k].m[14];
                newW[k] = fitted;
                std::vector<char> fromHand(n, 0);
                fromHand[k] = 1;
                Propagate(h, oldW, newW, fromHand, k);
            };
            // fingers set against the hand as it stands now (rel: each against the hand), over the fist by e
            const auto layFingers = [&](const std::vector<Mat>& rel, float e) {
                if (rel.size() != newW.size()) return;
                if (poseW.empty()) poseW = newW;
                poseW[k] = newW[k];   // (a pose made before for the other hand's weapon has this hand where it was before it was laid)
                for (const Hero::Finger& f : h.fingers[side])
                    for (const int b : f.bones) poseW[b] = Mul(rel[b], newW[k]);
                poseE[side] = e;
            };
            if (!holding) {   // let go (or the box unticked): eased back from the last hold
                LibGrabForget(side);
                if (!E.have) continue;
                const float e = expf(-kPoseBlend * (float)(nowS - E.releasedAt));
                if (!tracked || e < 0.02f || !in.grabWrap) {
                    E.have = false;
                    continue;
                }
                applyFit(E.r, E.shift, e);
                if (!E.fingerRel.empty()) layFingers(E.fingerRel, e);
                continue;
            }
            const V3 headM = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
            const float upmM = std::clamp(headM.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // (as ControllerPoint)
            const V3 am = ToModel(in.grabCapA[side], c, s), bm = ToModel(in.grabCapB[side], c, s);   // the held capsule: metres from the head, model axes
            const V3 axis = Sub(bm, am);
            const float len = Len(axis), rad = std::max(0.01f, in.grabCapR[side]);
            if (!(len > 1e-3f) || !std::isfinite(len)) continue;
            const V3 dir = Scale(axis, 1.0f / len);
            grippose::M3 e;
            if (!HandFrameAt(h, side, newW, &e)) continue;
            const grippose::M3 ep = griprig::ProperFrame(e, side);   // x to the fingers, y across the knuckles, z out of the palm
            // The palm laid on the body where the grab holds it, turned about the palm to face it (grabbodies::LayPalm). (Not the grip library's own laying: it
            // turns the hand ABOUT the shaft so the palm faces the axis - right for a weapon's thin shaft, but on a body's fat capsule it swung the palm round
            // the body away from where it took hold: the headset 0.183, the palm 12-21 cm from the held point.)
            const V3 amU = Add(headM, Scale(am, upmM)), bmU = Add(headM, Scale(bm, upmM));
            const float depth = 0.2f * Len(Sub(Pos(newW[ik]), Pos(newW[pk])));
            const grippose::V x0 = ep.Row(0);
            const float pa[3] = {palm.x, palm.y, palm.z}, fx[3] = {x0.x, x0.y, x0.z}, ca[3] = {amU.x, amU.y, amU.z}, cb[3] = {bmU.x, bmU.y, bmU.z};
            float to[3], rows9[9];
            if (!grabbodies::LayPalm(pa, fx, ca, cb, rad * upmM, depth, to, rows9)) continue;
            const V3 pm = {to[0], to[1], to[2]};
            grippose::M3 et;
            for (int i = 0; i < 9; ++i) et.m[i] = rows9[i];
            const float ein = 1.0f - expf(-kPoseBlend * (float)(nowS - E.heldSince));   // eased on at the toolkit's pose blend
            // the fingers: the grip library's hand step (the toolkit's dynamic pose) closes them round the body from the hand as laid
            D2rgHandIn hin{};
            hin.holds = 1;
            hin.mode = D2RG_MODE_TIGHT;
            const V3 palmRel = Scale(Sub(pm, headM), 1.0f / upmM);
            hin.handPos = {palmRel.x, palmRel.y, palmRel.z};
            hin.handRot = griprig::QuatOfRows(et);
            hin.flags = 0;   // (the hand stays turned as laid)
            hin.shaft.at = {am.x, am.y, am.z};
            hin.shaft.dir = {dir.x, dir.y, dir.z};
            const V3 up = Norm(std::fabs(dir.y) < 0.9f ? Cross(dir, V3{0, 1, 0}) : Cross(dir, V3{1, 0, 0}));
            hin.shaft.up = {up.x, up.y, up.z};
            hin.shaft.butt = 0.0;
            hin.shaft.tip = len;
            hin.shaft.nPieces = 1;
            hin.shaft.pieces[0] = {0.0, len, rad};
            LibGripResult res;
            const char* why = LibGrabRun(h, side, in, newW, upmM, hin, &res);
            libGripRes[side] = why ? LibGripResult{} : std::move(res);
            {   // why the library did not lay it, once for each reason
                static const char* told[2] = {nullptr, nullptr};
                if (why && told[side] != why) {
                    char b[260];
                    snprintf(b, sizeof b, "grab - the %s hand closes as a fist on the body (no grip library's fingers): %s", side ? "left" : "right", why);
                    weight::Say(b);
                }
                told[side] = why;
            }
            const grippose::M3 rr = grippose::Mul(grippose::Transpose(ep), et);
            gripfit::Rot r;
            for (int i = 0; i < 9; ++i) r.m[i] = rr.m[i];
            const V3 shift = Sub(pm, palm);
            applyFit(r, shift, ein);
            {   // where the palm is drawn now (the arm's reach may fall short of it), for grab_wire's log: the hands' frame, metres from the head
                const V3 drawnP = Scale(Add(Pos(newW[k]), Pos(newW[mk])), 0.5f);
                const V3 d = Scale(Sub(drawnP, headM), 1.0f / upmM);
                const V3 q = {d.x * c - d.z * s, d.y, d.x * s + d.z * c};
                const float head3[3] = {-q.x, q.y, -q.z};
                NoteGrabPalm(side, head3, why ? "our frame, the fist" : "our frame, the grip library's fingers");
            }
            E.have = true;
            E.r = r;
            E.shift = shift;
            E.fingerRel.clear();
            // the library's fingers, closed round the body (they ease closed in it), laid on the bones in the hand's frame as it stands now
            const LibGripResult& L = libGripRes[side];
            grippose::M3 e2;
            std::vector<grippose::M3> rows;
            if (!L.ok || !L.out.fingersOn || !HandFrameAt(h, side, newW, &e2) || !griprig::Compose(L.rig, L.mirror, griprig::ProperFrame(e2, side), L.out.finger, &rows)) continue;
            std::vector<Mat> laidW = newW;
            size_t idx = 0;
            bool ok = true;
            for (const Hero::Finger& f : h.fingers[side])
                for (const int b : f.bones) {
                    const int pa = h.parent[b];
                    if (pa < 0 || idx >= rows.size()) { ok = false; break; }
                    Mat nw = Mul(Mul(h.bind[b], InvAffine(h.bind[pa])), laidW[pa]);
                    for (int r2 = 0; r2 < 3; ++r2) {
                        const float l2 = (float)boneguard::RowLen(newW[b].m, r2);
                        for (int c2 = 0; c2 < 3; ++c2) nw.m[r2 * 4 + c2] = l2 * rows[idx].m[r2 * 3 + c2];
                        nw.m[r2 * 4 + 3] = 0.0f;
                    }
                    nw.m[15] = 1.0f;
                    for (float v : nw.m)
                        if (!std::isfinite(v)) ok = false;
                    laidW[b] = nw;
                    ++idx;
                }
            if (!ok) continue;
            const Mat handInv = InvAffine(newW[k]);
            E.fingerRel.assign(newW.size(), Mat{});
            for (const Hero::Finger& f : h.fingers[side])
                for (const int b : f.bones) E.fingerRel[b] = Mul(laidW[b], handInv);
            layFingers(E.fingerRel, 1.0f);
            libFingers[side] = true;
        }
    }
    if (wo.on && in.mode == 2 && in.weight.gripFit && in.weight.gripFingers && h.bindOk) {
        GripPoseKept kept;
        AcquireSRWLockShared(&g_gripPoseLock); kept = g_gripPose; ReleaseSRWLockShared(&g_gripPoseLock);
        static int told[2] = {0, 0};   // 0 nothing said, 1 the pose took, 2 it could not (said once for each change)
        for (int side = 0; side < 2; ++side) {
            const float t = std::clamp(wo.on01[side], 0.0f, 1.0f);
            if (side == wo.slot || !(t > 0.0f) || (side == 1 && leftOnStaff) || libFingers[side]) continue;   // (the slot's hand has the game's pose; staff_free_left's left keeps the game's grip; the library's hand has the library's)
            const char* why = nullptr;
            if (!kept.ok || kept.skel != h.skel || kept.type != in.weaponType || kept.side != (wo.slot & 1)) why = "no pose read off the game's hand for this weapon yet";
            else {
                if (poseW.empty()) poseW = newW;
                why = GripPoseOnto(h, side, kept, newW, poseW);
            }
            if (why) {
                if (told[side] != 2) { char b[220]; snprintf(b, sizeof b, "grip fingers - the %s hand keeps the squeeze's fist: %s", side ? "left" : "right", why); weight::Say(b); }
                told[side] = 2;
                continue;
            }
            poseE[side] = t * t * (3.0f - 2.0f * t);
            if (told[side] != 1) {
                char b[220];
                snprintf(b, sizeof b, "grip fingers - the %s hand takes the game's weapon-grip pose of the %s hand (weapon type %d)", side ? "left" : "right", wo.slot ? "left" : "right", in.weaponType);
                weight::Say(b);
            }
            told[side] = 1;
        }
    }
    if ((in.fist || ((in.weight.gripFit || wo.gripLib) && wo.on)) && in.mode == 2)
        for (int side = 0; side < 2; ++side) {
            if (poseE[side] >= 1.0f) continue;   // fully in the game's grip pose: the squeeze does not drive it
            const bool onWeapon = wo.on && wo.holds[side] && side != wo.slot;
            // (shoulder_draw handles the shield: the left hand is tied to the game's left slot only while the shield is on the left arm)
            const bool freeSide = in.handFree[side] || (in.shieldHolster && in.shieldArm >= 0 && side == 1 && in.shieldArm != 1);
            const bool free = wo.on ? (side == wo.slot ? !wo.holds[side] : freeSide || onWeapon) : freeSide;
            if (!in.fist && !onWeapon) continue;   // (fist off: only a hand on the weapon, by grip_fit)
            if (free && (in.handsValid & (1u << side)) && (in.grip[side] >= 0.0f || onWeapon) && !(side == 1 && leftOnStaff)) {   // on a shaft: the game's grip, not our fist (it wraps better, 2026-10-06)
                const float squeeze = in.weight.haveRawGrip ? in.weight.rawGrip[side] : in.grip[side];
                Fist(h, side, onWeapon ? ((in.weight.gripFit || wo.gripLib) ? gripfit::Close(squeeze) : 1.0f) : in.grip[side],
                     in.fistCap[side] >= 0.0f ? std::min(in.fistClose[h.cls >= 0 ? h.cls : 3], in.fistCap[side]) : in.fistClose[h.cls >= 0 ? h.cls : 3],
                     in.fistOpen[h.cls >= 0 ? h.cls : 3], oldW, newW);
                if (side == 1) leftFisted = true; else rightFisted = true;
            }
        }
    // ... and the pose over it, by how far the hand is on the weapon (smoothstep): the fingers and the thumb of the taking hand, bone by bone
    for (int side = 0; side < 2; ++side) {
        if (!(poseE[side] > 0.0f)) continue;
        for (const Hero::Finger& f : h.fingers[side])
            for (const int b : f.bones) {
                if (poseE[side] >= 1.0f) newW[b] = poseW[b];
                else BlendBoneHanded(newW[b], poseW[b], poseE[side], &newW[b]);
            }
        if (side == 1) leftFisted = true; else rightFisted = true;   // (the shield fist leaves a hand that is posed)
    }
    // [shield] forearm: the shield strapped to the left forearm (StrapShield) - after everything that places the arms and the weapons,
    // before the shield's place is told to shield_smite (its capsule follows the shield as drawn).
    // [hands] shoulder_draw (vr/shield_arm.h): the shield is on the arm shieldArm says - it follows the sword, so after a cross draw it is on the right forearm, mirrored.
    // g_shieldStrapped: 1 the left forearm, 3 the right, 2 put away (the attach bone shrunk to a point, as the game shrinks one that holds nothing; it is still the game's equipped shield).
    const int shieldSide = in.shieldArm == 0 ? 0 : 1;
    // The shield's bones: the game's left attach bone and shield_attach (which of them draws it on the screen is not the same in every rig, and the upper-body pass puts shield_attach back
    // on the left forearm every pose): whatever is done to the shield is done to both, each from its own bind pose. The one shield_attach names comes last (its numbers stand in the log).
    const int shieldBones[2] = {h.weapon[1], h.shield >= 0 && h.shield != h.weapon[1] ? h.shield : -1};
    const auto hideShield = [&]() {   // the bones shrunk to a point, as the game shrinks one that holds nothing; what hangs on them follows
        for (const int wb : shieldBones) {
            if (wb < 0) continue;
            const std::vector<Mat> before = newW;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) newW[wb].m[r * 4 + c] *= 0.001f;
            std::vector<char> fromS(n, 0);
            fromS[wb] = 1;
            Propagate(h, before, newW, fromS, wb);
        }
    };
    // [shield] heads_hang: a Necromancer's head on the left arm is not strapped - it hangs from the hand (its own bones swing it: PoseHead)
    const bool headHangs = in.headsHang && in.mode == 2 && in.shieldArm != 0 && in.shieldArm != 2;
    if (headHangs) {
        if (in.handsValid & 2u) HoldHead(h, oldW, newW);
        g_shieldStrapped.store(0);
    } else if (in.shieldForearm && in.mode == 2 && in.shieldArm == 2 && (shieldBones[0] >= 0 || shieldBones[1] >= 0)) {
        hideShield();
        g_shieldStrapped.store(2);
    } else if (in.shieldForearm && in.mode == 2 && (in.handsValid & (1u << shieldSide))) {
        bool strapped = false;
        for (const int wb : shieldBones)
            if (wb >= 0) strapped = StrapShield(h, in, oldW, newW, twistRoll[shieldSide], shieldSide, wb) || strapped;
        g_shieldStrapped.store(strapped ? (shieldSide == 0 ? 3 : 1) : -1);
        // [debug] shield_blink: the bone the shield hangs on is shrunk to a point for half a second in every two - if the shield on the screen blinks, it is this bone that draws it, and the
        // strap, the corrections, the put-away and the jump to the other arm all act on it; if it does not, the shield on the screen hangs on something else
        if (strapped && in.shieldBlink && (GetTickCount64() % 2000) < 500) hideShield();   // (the shield is the left side's bones' on either arm)
    } else {
        g_shieldStrapped.store(0);
    }
    // [hands] shield_fist: the left hand with a shield on it follows the controller's real grip too - the fingers only. Fist() rebuilds
    // the finger chains below the wrist (not the weapon attach bone the shield hangs on) after the arm is placed, so the shield and the
    // arm stay as the game draws them (vr/shield_fist.h). The weight model's inputs are not touched: that hand is told to it as busy.
    if (in.shieldFist && in.shieldArm != 2) {   // (put away: no shield on any hand)
        shieldfist::Want want;
        want.on = true;
        want.f4 = viewgate::F4();   // (a view change shows in the very next pose)
        want.shield = true;         // vrcam set shieldFist for a shield in the left slot (gamestate::LeftShield())
        want.arms = in.mode == 2;
        want.tracked = (in.handsValid & (1u << shieldSide)) != 0;   // the hand of the arm the shield is on (the left one unless shoulder_draw moved it)
        want.rawGrip = in.shieldRaw[shieldSide];
        want.layoutGrip = in.grip[shieldSide];
        want.closedAlready = shieldSide == 1 ? leftFisted : rightFisted;
        if (const float g = shieldfist::Grip(want); g >= 0.0f)
            Fist(h, shieldSide, g,
                 in.fistCap[shieldSide] >= 0.0f ? std::min(in.fistClose[h.cls >= 0 ? h.cls : 3], in.fistCap[shieldSide]) : in.fistClose[h.cls >= 0 ? h.cls : 3],
                 in.fistOpen[h.cls >= 0 ? h.cls : 3], oldW, newW);
    }

#if D2RVR_FIRST_PERSON
    // [bow] hand_pose: the hand that takes a bow's string is relaxed (the fist's open amount), not the game's animation of it,
    // and hooks its fingers on the string - the thumb loose - once the draw says the string is taken (or, with no draw
    // detector, as its grip closes). The bow hand keeps the game's animation of holding the bow.
    int handPosedSide = -1;   // the hand posed here, for the check that nothing after changes it (g_handPoseCurlOut)
    if (in.bowHandPose && in.mode == 2) {
        // Which hand takes the string: the draw's own word (the hand opposite the one the player holds the bow in), else vrcam's
        // (the same, from the aim hand), else - last - the wrist the game hung the bow on, which a bow with a quiver on the other
        // wrist (both attach bones shown, "R 1.00 L 1.00") cannot tell.
        bool drawing = false;
        int side = BowFeedSide(&drawing);
        const char* how = "the draw's feed";
        float attach[2] = {-1.0f, -1.0f};
        const int bowSide = BowSideOf(h, oldW, attach);   // (also for the log: the sizes show whether the attach bones can tell)
        g_handPoseAttach[0].store(attach[0]); g_handPoseAttach[1].store(attach[1]);
        if (side < 0 && (in.bowStringSide == 0 || in.bowStringSide == 1)) { side = in.bowStringSide; how = "the aim hand"; }
        if (side < 0 && bowSide >= 0) { side = bowSide ^ 1; how = "the bow's attach bones"; }
        const char* status = "posed";
        if (side < 0) status = "no string hand known (no draw feed, no aim hand, the attach bones tell nothing)";
        else if (!(in.handsValid & (1u << side))) status = "the string hand's controller is not tracked";
        else if (h.hand[side] < 0 || h.ind[side] < 0 || h.mid[side] < 0 || h.pnk[side] < 0 || h.fingers[side].empty()) status = "the hero's fingers for that hand were not found";
        else if (in.grabWrap && in.grabHolding[side]) status = "the string hand holds a body ([weight] grab_wrap lays it)";   // (its fingers are the grab's)
        else {
            // How far it closes: the string taken closes it whatever the controls read (the draw took them from the layout, which
            // then sees a grip let go), else the controller's own trigger and squeeze as the draw's wire read them, else the layout's
            // grip. Eased (bowhand::Ease): the fingers hook in 80 ms and let go in 180.
            bowhand::Reading rd;
            rd.drawing = drawing;
            rd.haveRaw = BowHandRaw(side, &rd.trigger, &rd.grip);
            rd.gameGrip = in.grip[side];
            const bowhand::Squeeze sq = bowhand::SqueezeOf(rd);
            // [bow] hook_game_pose: with the string taken the Amazon's right hand takes the pose of the game's own draw animation (HookPose) in
            // place of the fist table's hook - held as it is while the string is taken, whatever the controller's grip and trigger read, and
            // eased back to what the squeeze asks (the relaxed hand) on the release. Without it, the fist-table hook as before.
            std::vector<Mat> hookW;
            const bool game = in.bowHookGame && (hookW = newW, HookPose(h, side, newW, hookW, HookTune(in)));
            bowhand::Reading restRd = rd;
            restRd.drawing = false;
            const bowhand::Squeeze sqRest = bowhand::SqueezeOf(restRd);   // what the controller alone asks of the hand
            float used, wDraw = 0.0f;
            {
                static SRWLOCK easeLock = SRWLOCK_INIT;
                static bowhand::Ease ease, easeDraw;
                static int easeSide = -1;
                static ULONGLONG easeAt = 0;   // (QueryPerformanceCounter ticks)
                static LARGE_INTEGER freq{};
                if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                AcquireSRWLockExclusive(&easeLock);
                const float dt = easeAt ? (float)((double)((ULONGLONG)now.QuadPart - easeAt) / (double)freq.QuadPart) : 1.0f;
                const float target = game ? sqRest.k : sq.k, drawTarget = game && drawing ? 1.0f : 0.0f;
                const bool fresh = easeSide != side || dt > 0.5f;   // a pose starting afresh is where it is asked to be
                if (fresh) { ease.v = target; easeDraw.v = drawTarget; }
                else { ease.Step(target, dt); easeDraw.Step(drawTarget, dt); }
                easeSide = side;
                easeAt = (ULONGLONG)now.QuadPart;
                used = ease.v;
                wDraw = easeDraw.v;
                ReleaseSRWLockExclusive(&easeLock);
            }
            const float hook = std::clamp(in.bowHook, 0.0f, 1.0f), open = in.fistOpen[h.cls >= 0 ? h.cls : 3];
            float curl = -1.0f;
            Fist(h, side, used, hook, open, oldW, newW, bowhand::kThumbShare, &curl);
            if (game && wDraw > 0.001f) {
                const float e = wDraw * wDraw * (3.0f - 2.0f * wDraw);   // (eased at both ends: the fingers do not snap)
                for (const Hero::Finger& f : h.fingers[side])
                    if (f.kind >= 0)
                        for (const int b : f.bones) {
                            Mat blended;
                            bowhook::BlendBone(newW[b].m, hookW[b].m, e, blended.m);
                            newW[b] = blended;
                        }
                curl = Curl(h, side, newW);
            }
            handPosedSide = side;
            g_handPoseTarget.store(sq.k);
            g_handPoseUsed.store(game ? std::max(used, wDraw) : used);
            g_handPoseAmount.store(bowhand::Amount(used, open, hook));
            g_handPoseHook.store(hook);
            g_handPoseCurl.store(curl);
            g_handPoseSource.store((int)sq.source);
            g_handPoseGame.store(game ? wDraw : -1.0f);
        }
        g_handPoseWhy.store(status);
        g_handPoseSide.store(side);
        g_handPoseHow.store(how);
    }
#endif

    // First person: the head and the neck shrink into a point at the neck's
    // base, so a camera at the eyes sees out instead of the inside of the skull,
    // and looking down sees the chest, not a neck. [arms] head_shadow (vr/head_shadow.h): while the renderer's hooks hand the eyes'
    // passes a copy of this pose with the head shrunk, the pose itself keeps the head - the shadow passes draw it.
    if (in.hideHead && h.head >= 0) {
        const int pivot = h.neck >= 0 ? h.neck : h.head;
        static_assert(sizeof(Mat) == sizeof(float) * 16, "a bone matrix is 16 floats");
        if (!headshadow::Publish(self, h.inNeck.data(), n, pivot, in.along))
            headshadow::Shrink(reinterpret_cast<float(*)[16]>(newW.data()), n, h.inNeck.data(), pivot);
    } else {
        headshadow::Unpublish(self);
    }
    // [debug] walk_back_dump: which world poses the hero's meshes are skinned from - this one, or another of his skeleton (headshadow::SkinNote)
    if (g_walkBackDumpOn.load(std::memory_order_relaxed) && !in.along) {
        uintptr_t cur = 0, prev = 0;
        if (Get(self + 0x20, &cur) && Get(self + 0x28, &prev)) headshadow::NoteHeroRig(h.skel, cur, prev);
    }
#endif
#if D2RVR_FIRST_PERSON
    // [diag] the weapon bone as we leave it, against the grip: must stay 0 whatever the game animates
    if (in.mode == 2 && h.weapon[0] >= 0 && h.hand[0] >= 0 && h.head >= 0) {
        Grip g; AcquireSRWLockShared(&g_gripLock); g = g_grip; ReleaseSRWLockShared(&g_gripLock);
        if (g.ok) {
            const float headY = in.haveEye ? in.eyeModel[1] : Pos(oldW[h.head]).y;
            const float cm = 100.0f / (std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale);
            const Mat now = Mul(newW[h.weapon[0]], InvAffine(newW[h.hand[0]]));
            const V3 a = Norm(V3{now.m[4], now.m[5], now.m[6]}), b = Norm(V3{g.attach[0].m[4], g.attach[0].m[5], g.attach[0].m[6]});
            g_diag[10].store(Len(Sub(Pos(now), Pos(g.attach[0]))) * cm);
            g_diag[11].store(acosf(std::clamp(Dot(a, b), -1.0f, 1.0f)) * 57.2958f);
        }
    }
    if (h.weapon[0] >= 0) {
        AcquireSRWLockExclusive(&g_rAttachLock);
        g_rAttach = newW[h.weapon[0]];
        if (in.bothAttach) { g_diagOld = oldW; g_diagNew = newW; g_diagWrist = h.hand[0]; g_diagAttach = h.weapon[0]; g_diagSkel = h.skel; }
        ReleaseSRWLockExclusive(&g_rAttachLock);
    }
    if ((in.bowRig || in.bowString) && in.mode == 2) NoteBowHands(self, h, in, oldW, newW, c, s, anchor);   // [bow] rig_pose, [hands] bow_draw: the bow's own rig is posed from this
    if (in.shrinkBone[0]) {   // [debug] shrink_bone: which bone does the weapon hang on? shrink it and see
        AcquireSRWLockShared(&g_namesLock);
        const auto it = g_boneNames.find(h.skel);
        if (it != g_boneNames.end())
            for (int i = 0; i < n && i < (int)it->second.size(); ++i)
                if (const std::string& nm = it->second[i]; !nm.empty() &&
                    (","+ std::string(in.shrinkBone) + ",").find("," + nm + ",") != std::string::npos)   // a comma list
                    for (int r = 0; r < 3; ++r) for (int c2 = 0; c2 < 3; ++c2) newW[i].m[r * 4 + c2] *= 0.001f;
        ReleaseSRWLockShared(&g_namesLock);
    }
#endif
#if D2RVR_FIRST_PERSON
    // [gameplay] shield_smite: the shield hangs on the left hand's weapon attach bone; its place in the render
    // world is that bone's matrix through the hero's own (the place and the axis the swing test builds the
    // shield's capsule from, bearstonem/d2r-pcvr's ShieldContactTick). Only while shield_smite asked for it lately.
    if (in.mode == 2 && h.weapon[1] >= 0 && GetTickCount64() - g_shieldWantedAt.load() < 1000) {
        ShieldPose pose{};
        ULONGLONG at = 0;
        HeroXform hx;
        Mat model;
        if (HeroTransform(&hx) && hx.self == self && (hx.parent & 0xFFFFFu) == 0xFFFFFu &&
            SafeCopy(model.m, reinterpret_cast<const void*>(hx.comp), sizeof model)) {
            PoseModel(model);
            const Mat world = Mul(newW[h.weapon[1]], model);
            const V3 origin = Pos(world), axis = Norm({world.m[4], world.m[5], world.m[6]}), player = Pos(model);
            if (Len({world.m[4], world.m[5], world.m[6]}) > 0.3f &&
                std::isfinite(origin.x + origin.y + origin.z + axis.x + axis.y + axis.z + player.x + player.y + player.z)) {
                pose = {{origin.x, origin.y, origin.z}, {axis.x, axis.y, axis.z}, {player.x, player.y, player.z}, HeroGen()};
                at = GetTickCount64();
            }
        }
        AcquireSRWLockExclusive(&g_gunBoneLock); g_shieldPose = pose; g_shieldAt = at; ReleaseSRWLockExclusive(&g_gunBoneLock);
    }
    if (handPosedSide >= 0) g_handPoseCurlOut.store(Curl(h, handPosedSide, newW));   // [bow] hand_pose: the fingers as they are written
#endif
    // [hands] shoulder_draw_ranged: the bow or crossbow put away behind a shoulder is not drawn - the attach bone it hangs on (the one the game shows, else the slot's) is shrunk to a point,
    // as the game shrinks one that holds nothing; it is still the game's equipped weapon.
#if D2RVR_FIRST_PERSON
    if (in.rangedStowed && in.mode == 2) HideRanged(h, in, oldW, newW);
    if (in.flailChain && in.mode == 2) NoteFlailHands(self, h, in, newW);   // [flail] chain: the hands as the flail's chain hangs off them
    if (in.headsHang && in.mode == 2) NoteHeadHands(self, h, in, newW);     // [shield] heads_hang: the hand as the head hangs off it
#endif
#if D2RVR_FIRST_PERSON
    // [body] under_camera: the up part of the move (t_poseUp, above), now that every bone is posed
    if (t_poseUp != 0.0f)
        for (int i = 0; i < n; ++i) newW[i].m[13] += t_poseUp;
    t_poseUp = 0.0f;
#endif
    // A bone that is not a number (a NaN carried down a chain by some turn that had no direction) would take its whole subtree out of the picture; the game's own pose of it stands in.
    {
        int nan = 0;
        for (int i = 0; i < n; ++i)
            if (!boneguard::Finite(newW[i].m)) { newW[i] = oldW[i]; ++nan; }
        if (nan) {
            static bool told = false;
            if (!told) {
                told = true;
                char b[120];
                snprintf(b, sizeof b, "%d bone matrices of the pose were not numbers: the game's own stand in (told once)", nan);
                weight::Say(b);
            }
        }
    }
#if D2RVR_FIRST_PERSON
    if ((in.armWorldLate || in.armBox) && h.head >= 0) {
        // [hud] hand_vitals_orb_drawn_hand: the arm in the WORLD (HeroArmWorld) read again from the hand as it is drawn at last - after grab_wrap laid it on
        // what it holds and the weight model fitted it onto the weapon. The forearms' read above comes before them, so a hand holding an NPC carried its wrist
        // medallions where the IK had put it, turned as the controller (the headset 0.185: «медальоны остаются на контроллере»). Only the world fields: the
        // hand-frame forearm, the palm and the cast hand as before. No world matrix this pose: the read above stands.
        HeroXform hx;
        Mat model;
        const char* why = "";
        const bool modelOk = HeroTransform(&hx, &why) && hx.self == self && (hx.parent & 0xFFFFFu) == 0xFFFFFu && SafeCopy(model.m, (const void*)hx.comp, sizeof model);
        if (modelOk) PoseModel(model);   // [body] under_camera: as the arm will be drawn
        // [hud] hand_vitals_orb_on_bone: the forearm bones' boxes off the hero's own meshes - his parts looked at once a second, the boxes read again only when they change
        uintptr_t inst = 0, skelp = 0;
        if (in.armBox && modelOk && GetTickCount64() - g_armBoxes.lookAt >= 1000 && Get(self + 0x08, &inst) && Get(inst + 0x08, &skelp)) {
            g_armBoxes.lookAt = GetTickCount64();
            uintptr_t models[monsterrig::kMaxModels] = {};
            uint64_t key = 0;
            const int nModels = HeroPartModels(hx.entity, models, monsterrig::kMaxModels, &key);
            if (nModels > 0 && (key != g_armBoxes.key || skelp != g_armBoxes.skel)) {
                const auto read = [](uintptr_t a, void* d, size_t bytes) { return a > 0x10000 && SafeCopy(d, (const void*)a, bytes); };
                static monsterrig::Boxes boxes;   // (large: the pose thread's alone)
                int nMeshes = 0;
                monsterrig::BoxesOfModels(read, models, nModels, skelp, &boxes, &nMeshes);
                const bool loading = !boxes.n && std::strcmp(boxes.why, "the model is not loaded yet") == 0;   // (asked again, as a monster's)
                if (!loading) {
                    g_armBoxes.key = key;
                    g_armBoxes.skel = skelp;
                }
                char note[400];
                int w = snprintf(note, sizeof note, "wrist medallions on the bone ([hud] hand_vitals_orb_on_bone): the hero's meshes (%d parts, %d meshes) give", nModels, nMeshes);
                for (int side = 0; side < 2; ++side) {
                    g_armBoxes.ok[side] = false;
                    for (int i = 0; i < boxes.n && h.lwr[side] >= 0; ++i)
                        if (boxes.b[i].bone == h.lwr[side]) {
                            g_armBoxes.ok[side] = true;
                            memcpy(g_armBoxes.mn[side], boxes.b[i].min, sizeof boxes.b[i].min);
                            memcpy(g_armBoxes.mx[side], boxes.b[i].max, sizeof boxes.b[i].max);
                        }
                    const float* a = g_armBoxes.mn[side];
                    const float* b = g_armBoxes.mx[side];
                    if (w > 0 && w < (int)sizeof note)
                        w += g_armBoxes.ok[side] ? snprintf(note + w, sizeof note - w, " the %s forearm a box %.2f x %.2f x %.2f (its bone's units);", side ? "left" : "right", b[0] - a[0], b[1] - a[1], b[2] - a[2])
                                                 : snprintf(note + w, sizeof note - w, " the %s forearm NO box (%s): the medallion where its sliders put it;", side ? "left" : "right",
                                                            boxes.n ? "its bone has none" : boxes.why && *boxes.why ? boxes.why : "nothing read");
                }
                AcquireSRWLockExclusive(&g_armNoteLock);
                if (g_armNote.size() < 3000) g_armNote += (g_armNote.empty() ? "" : "\n") + std::string(note);
                ReleaseSRWLockExclusive(&g_armNoteLock);
            }
        }
        for (int side = 0; side < 2 && modelOk; ++side) {
            const int l = h.lwr[side], k = h.hand[side], mk = h.mid[side], ik = h.ind[side], pk = h.pnk[side];
            if (l < 0 || k < 0) continue;
            const V3 E = Pos(newW[l]), W = Pos(newW[k]);
            const V3 across = PalmAcross(h, side, newW, Norm(Sub(W, E)));
            V3 pointU{0, 0, 0}, faceU{0, 0, 0};
            const bool handW = mk >= 0 && ik >= 0 && pk >= 0;
            if (handW) {
                const V3 point = Sub(Pos(newW[mk]), W), line = Sub(Pos(newW[ik]), Pos(newW[pk]));
                pointU = Norm(point);
                faceU = Norm(Scale(Cross(point, line), side == 0 ? -1.0f : 1.0f));   // out of the palm, as Fist has it
            }
            const V3 ew = Through(model, E), ww = Through(model, W);
            const V3 xw = Norm(RotOnly(across, model)), pw = Norm(RotOnly(pointU, model)), fw = Norm(RotOnly(faceU, model));
            if (!std::isfinite(ew.x + ew.y + ew.z + ww.x + ww.y + ww.z + xw.x + xw.y + xw.z + pw.x + pw.y + pw.z + fw.x + fw.y + fw.z)) continue;
            // [hud] hand_vitals_orb_on_bone: the forearm bone's box as it stands now - its middle and half sides through the bone, then the hero's model-to-world
            bool boxOk = false;
            V3 bc{}, bh[3]{};
            if (in.armBox && g_armBoxes.ok[side]) {
                const float* mn = g_armBoxes.mn[side];
                const float* mx = g_armBoxes.mx[side];
                const V3 mid{0.5f * (mn[0] + mx[0]), 0.5f * (mn[1] + mx[1]), 0.5f * (mn[2] + mx[2])};
                bc = Through(model, Through(newW[l], mid));
                boxOk = std::isfinite(bc.x + bc.y + bc.z);
                for (int i = 0; i < 3 && boxOk; ++i) {
                    V3 end = mid;
                    (i == 0 ? end.x : i == 1 ? end.y : end.z) += 0.5f * (mx[i] - mn[i]);
                    bh[i] = Sub(Through(model, Through(newW[l], end)), bc);
                    boxOk = std::isfinite(bh[i].x + bh[i].y + bh[i].z);
                }
            }
            AcquireSRWLockExclusive(&g_forearmLock);
            ForearmOut& f = g_forearm[side];
            if (f.at) {   // (written by the read above this very pose)
                f.boxOk = boxOk;
                if (boxOk) {
                    f.boxCW[0] = bc.x; f.boxCW[1] = bc.y; f.boxCW[2] = bc.z;
                    for (int i = 0; i < 3; ++i) { f.boxHW[i][0] = bh[i].x; f.boxHW[i][1] = bh[i].y; f.boxHW[i][2] = bh[i].z; }
                }
            }
            if (f.at && in.armWorldLate) {
                f.elbowW[0] = ew.x; f.elbowW[1] = ew.y; f.elbowW[2] = ew.z;
                f.wristW[0] = ww.x; f.wristW[1] = ww.y; f.wristW[2] = ww.z;
                f.acrossW[0] = xw.x; f.acrossW[1] = xw.y; f.acrossW[2] = xw.z;
                f.pointW[0] = pw.x; f.pointW[1] = pw.y; f.pointW[2] = pw.z;
                f.faceW[0] = fw.x; f.faceW[1] = fw.y; f.faceW[2] = fw.z;
                f.handW = handW;
                f.worldOk = true;
                f.worldAt = GetTickCount64();
            }
            ReleaseSRWLockExclusive(&g_forearmLock);
        }
    }
    {   // [multiplayer] pose_share (formats 5, 6): the bones the others hang our hero's weapon and hands on - both attach bones, both hands and elbows, the head bone -
        // as they are written now, after all that moves them (the hands fitted onto the weapon, the shield strapped, the body under the camera); the head's
        // turn-only frame from the eye, metres, with our model's units a metre (DrawnAttach, DrawnHands). Read before at the bone axes, a hand the weight model
        // fitted onto the weapon later, or a strapped shield, went to the others where it was not drawn.
        DrawnAttachOut da;
        da.main = axesSide;
        const V3 eye = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head >= 0 ? h.head : 0]);
        const float e3[3] = {eye.x, eye.y, eye.z};
        da.upm = std::clamp(eye.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
        const auto read = [&](int bone, float origin[3], float rows[3][3]) {
            if (bone < 0 || bone >= n) return false;
            const Mat& m = newW[bone];
            const float p[3] = {m.m[12], m.m[13], m.m[14]};
            remotepose::PointToHead(p, e3, c, s, da.upm, origin);
            bool fine = std::isfinite(origin[0] + origin[1] + origin[2]);
            for (int i = 0; fine && i < 3; ++i) {
                const V3 row = {m.m[i * 4], m.m[i * 4 + 1], m.m[i * 4 + 2]};
                fine = Len(row) > 0.001f;   // (a point: the game hides a slot that holds nothing)
                const V3 d = Norm(row);
                const float d3[3] = {d.x, d.y, d.z};
                remotepose::DirToHead(d3, c, s, rows[i]);
            }
            return fine;
        };
        for (int slot = 0; slot < 2; ++slot) da.ok[slot] = read(h.weapon[slot], da.origin[slot], da.rows[slot]);
        float rows[3][3];
        bool hands = read(h.head, da.headBone, rows);
        for (int side = 0; side < 2 && hands; ++side) hands = read(h.hand[side], da.hand[side], da.handRows[side]) && read(h.lwr[side], da.elbow[side], rows);
        da.handsOk = hands;
        da.at = GetTickCount64();
        AcquireSRWLockExclusive(&g_drawnAttachLock); g_drawnAttach = da; ReleaseSRWLockExclusive(&g_drawnAttachLock);
    }
#endif
    SafeWrite((void*)mats, newW.data(), sizeof(Mat) * n);
}

#if D2RVR_FIRST_PERSON
// A held weapon has a skeleton of its own (root > translate > anim > cog > alt_cog, the club's; bows
// add their string), its model skinned to it, and the game animates it with the hero: walking turned a
// great axe along the arm inside the hand, whatever its attach bone did (2026-10-06 - the attach bone
// was held fast, the 2H diag showed it never moved to the other wrist). With the arms on the controllers
// (in.holdGrip) a melee weapon's own skeleton keeps the pose it had while the hero's animation held it
// as the grip does (g_weaponCalm, standing) - not its bind pose: that lay the axe along the forearm.
// Bows and crossbows keep theirs animated (the string). Every unit's held weapons, NPCs' too - nothing
// yet tells the hero's apart.
struct WeaponItem { bool held = false; int count = 0; std::vector<Mat> pose, last; bool havePose = false; ULONGLONG stillSince = 0; };
SRWLOCK g_itemLock = SRWLOCK_INIT;
std::map<std::pair<uintptr_t, uintptr_t>, WeaponItem> g_items;
// for vrcam's log: weapon skeletons met, poses remembered / held, the last one's model and bone count
std::atomic<uint32_t> g_itemNew{0}, g_itemCaught{0}, g_itemHeld{0}, g_itemCalls{0};
SRWLOCK g_itemNameLock = SRWLOCK_INIT;
std::string g_itemName;
std::atomic<int> g_itemBones{0};
// held-weapon SkeletonInstances met lately (for the entity diag): self -> when
SRWLOCK g_itemSelfLock = SRWLOCK_INIT;
std::map<uintptr_t, ULONGLONG> g_itemSelves;

void HoldWeaponItem(uintptr_t self) {
    bool hold;
    AcquireSRWLockShared(&g_inLock); hold = g_in.mode == 2 && g_in.holdGrip; ReleaseSRWLockShared(&g_inLock);
    if (!hold) return;
    uintptr_t modelInst = 0, skel = 0;
    if (!Get(self + 0x08, &modelInst) || !Get(modelInst + 0x08, &skel)) return;
    const auto key = std::make_pair(self, skel);
    WeaponItem w; bool have = false;
    AcquireSRWLockShared(&g_itemLock);
    if (const auto it = g_items.find(key); it != g_items.end()) { w = it->second; have = true; }
    ReleaseSRWLockShared(&g_itemLock);
    if (!have) {
        uintptr_t nameObj = 0, nameP = 0, bones = 0; int32_t count = 0;
        Get(self, &nameObj); Get(nameObj + 8, &nameP);
        const std::string model = Str(nameP);
        if (model.find("/items/") != std::string::npos && model.find("dropped") == std::string::npos) {
            Get(skel + 8, &count);
            AcquireSRWLockExclusive(&g_itemNameLock); g_itemName = model; ReleaseSRWLockExclusive(&g_itemNameLock);
            g_itemBones.store(count);
            g_itemNew++;
        }
        if (model.find("/items/weapon/") != std::string::npos && model.find("/bow/") == std::string::npos &&
            model.find("crossbow") == std::string::npos && Get(skel + 8, &count) && Get(skel + 0xC, &bones) && count > 0 && count <= 64) {
            w.held = true; w.count = count;
        }
        AcquireSRWLockExclusive(&g_itemLock);
        if (g_items.size() > 4096) g_items.clear();
        g_items[key] = w;
        ReleaseSRWLockExclusive(&g_itemLock);
    }
    if (!w.held) return;
    g_itemCalls++;
    {
        AcquireSRWLockExclusive(&g_itemSelfLock);
        if (g_itemSelves.size() > 512) g_itemSelves.clear();
        g_itemSelves[self] = GetTickCount64();
        ReleaseSRWLockExclusive(&g_itemSelfLock);
    }
    uintptr_t world = 0, mats = 0; int32_t n = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &n) || !Get(world + 4, &mats) || n != w.count || !mats) return;
    // The pose to keep: one the game's animation has held still for 0.4 s (standing - walking never
    // rests). g_weaponCalm (the attach bone in its grip) hardly ever read true: the grip may be taken
    // in another pose, and a stale pose was held instead (09:50).
    std::vector<Mat> now(n);
    if (!SafeCopy(now.data(), (const void*)mats, sizeof(Mat) * n)) return;
    bool still = (int)w.last.size() == n;
    for (int i = 0; i < n && still; ++i) {
        for (int r = 0; r < 3 && still; ++r) {
            const V3 a = Norm(V3{now[i].m[r * 4], now[i].m[r * 4 + 1], now[i].m[r * 4 + 2]});
            const V3 b = Norm(V3{w.last[i].m[r * 4], w.last[i].m[r * 4 + 1], w.last[i].m[r * 4 + 2]});
            still = Dot(a, b) > 0.99985f;   // under 1 deg since the last call
        }
        still = still && Len(Sub(Pos(now[i]), Pos(w.last[i]))) < 0.01f;
    }
    const ULONGLONG t = GetTickCount64();
    w.stillSince = still ? (w.stillSince ? w.stillSince : t) : 0;
    w.last = now;
    if (w.stillSince && t - w.stillSince > 400) { w.pose = now; w.havePose = true; g_itemCaught++; }
    AcquireSRWLockExclusive(&g_itemLock); g_items[key] = w; ReleaseSRWLockExclusive(&g_itemLock);
    if (w.havePose && !(w.stillSince && t - w.stillSince > 400)) {
        SafeWrite((void*)mats, w.pose.data(), sizeof(Mat) * n);
        g_itemHeld++;
    }
}
#endif

// Which of the rigs from a player's folder is ours (2026-10-13). Learn took every one, so in a
// game with others each of them was posed with our arms and its head hidden, and so was our
// own corpse; each was also noted as the hero (NoteHero), the facing's SkeletonInstance going
// back and forth between them. Now a rig is ours only
//  - standing: its posed head above a third of its standing height (a corpse lies, a dying
//    hero falls);
//  - within 12 units of the camera's look-at: the look-at trails the hero on a spring, several
//    units in a run, so the same 12 as the facing's (ReadHeroMatrix in vrcam);
//  - and the one taken: once one is, it stays ours wherever it runs - near the trailing look-at
//    a player following us may be nearer than we are - with 3 s of grace while it is not posed.
//    A new rig where it stood (within a unit, in its first poses: the model rebuilt on a weapon
//    swap or at an area's border) takes over at once. With none taken (the start, after a
//    waypoint or a death) the nearest to the look-at is, a rig waiting for its second pose so
//    that every rig of that frame is known; and while the look-at stands still - caught up with
//    the hero - one nearer to it than ours by a unit takes over, which mends a wrong choice.
// Where the renderer's ECS does not read (another game build) or the look-at is not known, the
// standing test alone.
struct Body { float x = 0.0f, z = 0.0f, dist = 0.0f; ULONGLONG at = 0; int poses = 0; };
SRWLOCK g_bodiesLock = SRWLOCK_INIT;
std::map<uintptr_t, Body> g_bodies;   // the standing rigs near the look-at, by SkeletonInstance
uintptr_t g_ours = 0;                 // the one taken

// What a player's rig is to the doll (Arms): taken - ours, the hero (NoteHero: the facing's chain follows it); along - posed with our arms too, but not taken (the
// facing stays on the rig taken); not ours. [hands] doll_rebuild (on): the hero's model rebuilt - a weapon swap, a weapon taken from or put behind a shoulder,
// another shield or bow - is a new SkeletonInstance of the same entity, and for a moment (the new model's parts loading: half a second, sometimes more) the game
// poses the old one and the new one both, either of them the one drawn. The doll let go for that moment (the author, 2026-10-21: "especially on a weapon
// swap, about half a second, sometimes more"): the new rig was taken at its first pose and the old one, still drawn, turned away; or the new one not yet in
// the hero's SkeletonComponent read as "no entity" for 500 ms, then, the hero having moved a unit meanwhile, was no rebuild of ours and waited out the old
// one's 3 s. Now both are posed while both are: the rig ours was rebuilt from stays along while the game poses it; a rig no entity holds (the one being made,
// or a menu's model) is along while ours is posed; a rebuild may lie as far from ours as the hero ran since ours was posed (20 units a second - a unit is ~0.27 m, a quick run; 4 at most);
// and an instance with no entity is looked for again after 50 ms, not 500. 0: as before 0.176 (one rig, the first-pose rebuild within a unit).
enum class Doll { Not, Ours, Along };
std::atomic<bool> g_dollRebuild{true};   // [hands] doll_rebuild (Set)
uintptr_t g_oursBefore = 0;              // the rig ours was rebuilt from (under g_bodiesLock)
ULONGLONG g_oursBeforeAt = 0, g_oursBeforeSeen = 0;   // when ours was rebuilt from it, when the game posed it last

// why (when given, [debug] doll_trace): the reason a rig is not taken, in words with its numbers.
Doll OursNow(uintptr_t self, const Hero& hero, char* why = nullptr, size_t whyCap = 0, bool trace = true) {   // trace: [debug] doll_trace (the rebuild notes)
    auto say = [&](Doll d, const char* fmt, auto... a) { if (why && whyCap) snprintf(why, whyCap, fmt, a...); return d; };
    const bool rebuildHold = g_dollRebuild.load(std::memory_order_relaxed);
    uintptr_t world = 0, mats = 0;
    float head[16];
    const float standing = hero.head >= 0 && hero.head < (int)hero.bind.size() ? hero.bind[hero.head].m[13] : 0.0f;
    if (standing > 2.0f && Get(self + 0x20, &world) && Get(world + 4, &mats) &&
        SafeCopy(head, (const void*)(mats + (uintptr_t)hero.head * 64), sizeof head) &&
        !(std::isfinite(head[13]) && head[13] > 0.35f * standing)) {
        AcquireSRWLockExclusive(&g_bodiesLock); g_bodies.erase(self); ReleaseSRWLockExclusive(&g_bodiesLock);
        return say(Doll::Not, "its head is down - posed at %.2f of the standing %.2f (under 35%%: lying, a corpse, a fall)", head[13], standing);
    }
    float L[3];
    ULONGLONG lookAt = 0, stillSince = 0;
    float x = 0.0f, z = 0.0f;
    const ULONGLONG now = GetTickCount64();
    bool noEntity = false;
    const bool placed = ecs::BodyPlace(self, &x, &z, &noEntity, rebuildHold ? 50 : 500);
    // A rig no entity of the renderer's world holds, while ours is posed: along, not taken - the facing's chain would read "the hero's SkeletonInstance is in
    // no SkeletonComponent" for it (a menu's model at a game's exit took the hero so, 2026-10-10 / 10-21 logs), and the rig being made at a swap is drawn too.
    if (noEntity && rebuildHold) {
        AcquireSRWLockExclusive(&g_bodiesLock);
        const auto o = g_bodies.find(g_ours);
        const bool oursLately = g_ours && g_ours != self && o != g_bodies.end() && now - o->second.at < 3000;
        const uintptr_t ours = g_ours;
        if (oursLately && self == g_oursBefore) g_oursBeforeSeen = now;   // (the rig ours was rebuilt from, out of the SkeletonComponent now: still posed)
        ReleaseSRWLockExclusive(&g_bodiesLock);
        if (oursLately) return say(Doll::Along, "no entity of the renderer's world holds it (being made, or a menu's model) while %p is ours", (void*)ours);
    }
    // Where the renderer's ECS does not read or the look-at is not known: the standing test alone.
    if (!HeroPos(L, &lookAt, &stillSince) || now - lookAt > 1000 || !placed) return Doll::Ours;
    AcquireSRWLockExclusive(&g_bodiesLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_bodiesLock); } } unlock;
    Body& me = g_bodies[self];
    me.x = x; me.z = z; me.at = now; if (me.poses < 1000) ++me.poses;
    // every one's distance from the look-at now; gone: not posed lately, or too far
    for (auto it = g_bodies.begin(); it != g_bodies.end();) {
        Body& b = it->second;
        b.dist = std::sqrt((b.x - L[0]) * (b.x - L[0]) + (b.z - L[2]) * (b.z - L[2]));
        if (now - b.at > (it->first == g_ours ? 3000u : 100u) || !(b.dist < 12.0f)) it = g_bodies.erase(it); else ++it;
    }
    const auto mine = g_bodies.find(self);
    if (mine == g_bodies.end())
        return say(Doll::Not, "%.1f units from the camera's look-at (%.1f %.1f; the rig at %.1f %.1f) - 12 at most", std::sqrt((x - L[0]) * (x - L[0]) + (z - L[2]) * (z - L[2])),
                   L[0], L[2], x, z);
    const auto ours = g_bodies.find(g_ours);
    if (ours == g_bodies.end()) g_ours = 0;
    if (self == g_ours) {
        if (g_oursBefore && g_oursBefore != self && !g_bodies.count(g_oursBefore)) {   // the rig it was rebuilt from is not posed any more: the swap's overlap is over
            if (why && trace) NoteDoll("doll: the rig ours was rebuilt from (%p) is not posed any more - the two were posed together for %llu ms", (void*)g_oursBefore,
                              (unsigned long long)(g_oursBeforeSeen - std::min(g_oursBeforeSeen, g_oursBeforeAt)));
            g_oursBefore = 0;
        }
        return Doll::Ours;
    }
    if (self == g_oursBefore) g_oursBeforeSeen = now;
    if (rebuildHold && self == g_oursBefore && now - g_oursBeforeAt < 10000)   // still posed by the game after ours was rebuilt from it: drawn, maybe
        return say(Doll::Along, "the rig ours (%p) was rebuilt from, %llu ms ago - still posed by the game", (void*)g_ours, (unsigned long long)(now - g_oursBeforeAt));
    const Body& b = mine->second;
    if (g_ours) {
        const Body& o = ours->second;
        const float apart = std::sqrt((b.x - o.x) * (b.x - o.x) + (b.z - o.z) * (b.z - o.z));
        const float reach = rebuildHold ? std::min(4.0f, 1.0f + 0.02f * (float)(now - std::min(now, o.at))) : 0.0f;   // as far as he ran since ours was posed
        const bool rebuilt = b.poses <= 3 && (rebuildHold ? apart < reach : fabsf(b.x - o.x) < 1.0f && fabsf(b.z - o.z) < 1.0f);
        const bool mends = now - stillSince > 500 && b.dist + 1.0f < o.dist;
        if (!rebuilt && !mends)
            return say(Doll::Not, "another rig is taken (%p, %.1f units from the look-at, posed %llu ms ago; this one %.1f) - this is not its rebuild (%d poses, %.1f units "
                       "from it) nor nearer by a unit with the look-at still", (void*)g_ours, o.dist, (unsigned long long)(now - std::min(now, o.at)), b.dist, b.poses, apart);
        if (rebuilt) {
            if (why && trace) NoteDoll("doll: the hero's model rebuilt (a weapon swap, a weapon to or from a shoulder, another shield or bow) - rig %p taken from %p, %.1f units "
                              "apart, %llu ms after %p was posed last%s", (void*)self, (void*)g_ours, apart, (unsigned long long)(now - std::min(now, o.at)), (void*)g_ours,
                              rebuildHold ? "; the old one posed along while the game poses it" : "");
            g_oursBefore = g_ours; g_oursBeforeAt = g_oursBeforeSeen = now;
        }
    } else {
        if (b.poses < 2) return say(Doll::Not, "none taken yet: waiting for its second pose (every rig of a frame known)");   // every rig of a frame known
        for (const auto& [s, other] : g_bodies)
            if (s != self && now - other.at <= 100 && other.dist < b.dist)
                return say(Doll::Not, "none taken yet: another rig is nearer the look-at (%p, %.1f units; this one %.1f)", (void*)s, other.dist, b.dist);
    }
    g_ours = self;
    return Doll::Ours;
}

#if D2RVR_FIRST_PERSON
// ---- [bow] rig_pose: the hero's bow, its own string, limbs and arrow, posed by the pull ----
// The bow in his hand is a skeleton of its own - one of the layouts of vr/bow_layouts.h, the 15 bows a hero holds - and the
// game animates its string when he shoots. While the draw detector says the string is drawn, and for
// 0.7 s after the release, the bones after the cog are set from the hands instead: the string at the
// string hand, the limbs folded toward it, the arrow on the string; after the release the rest pose,
// so that the game's own wind-up (it starts when the shot is sent) is not played a second time.
// Another bow (an NPC's, a dropped one, another rig) is not touched, and nothing here reaches the
// game's state: only this one skeleton's matrices for this pose. Ticked off, none of it runs.
// The idea is bearstonem/d2r-pcvr's (MIT), vr/bow_pose.h and the bow block of its skeletons.cpp.
static_assert(sizeof(bowrig::Mat) == sizeof(Mat), "the rig's matrices are the game's bone matrices");

std::atomic<bool> g_bowRigOn{false};                 // Set(): [bow] rig_pose with the arms on the controllers
std::atomic<bool> g_bowStringOn{false};              // Set(): [hands] bow_draw wants the posed bow's string place (BowString)
std::atomic<bool> g_bowStaveOn{false};               // Set(): [bow] quiver_orbs wants the bow's stave (BowStave)
std::atomic<bool> g_bowArrowOn{false};               // Set(): [bow] arrow_fx wants the bow's arrow as it is drawn (BowArrow)
// The arrow as it is drawn in the frame of the hands: its nock and head (3 + 3), whether our pose drew it, when; and how its head was found (for the log).
SRWLOCK g_bowArrowLock = SRWLOCK_INIT;
float g_bowArrowAt[6] = {};
bool g_bowArrowDrawn = false;
ULONGLONG g_bowArrowStamp = 0;
std::string g_bowArrowWho;
// The bow's stave in the frame of the hands: the riser's middle, the upper limb's end, the unit direction toward the archer (3 + 3 + 3), and when.
SRWLOCK g_bowStaveLock = SRWLOCK_INIT;
float g_bowStaveAt[9] = {};
ULONGLONG g_bowStaveStamp = 0;
// Where the posed bow's string rests, in the frame of the hands (metres from the head; Input::hand's, no ini offset), and when.
SRWLOCK g_bowStringLock = SRWLOCK_INIT;
float g_bowStringAt[3] = {};
ULONGLONG g_bowStringStamp = 0;
std::atomic<ULONGLONG> g_bowDrawnAt{0};              // the string was last seen drawn (the feed, or [debug] bow_rig_grip)
std::atomic<ULONGLONG> g_bowPosedAt{0};
std::atomic<const char*> g_bowWhy{"the string has not been drawn yet"};

SRWLOCK g_bowLock = SRWLOCK_INIT;                    // the feed and the hands below
BowDraw g_bowFeed;
ULONGLONG g_bowFeedAt = 0;
// What this pose of the hero gives the bow's: the bone it hangs on as we left it, and the controllers.
struct BowHands {
    ULONGLONG at = 0;
    uintptr_t hero = 0;
    bool placed = false;                             // attach is the bow's bone
    int bowSide = 1;                                 // the wrist the game hung it on, 0 right 1 left
    bool hands = false;                              // both controllers tracked: control
    bool gripPull = false;                           // [debug] bow_rig_grip: the string hand's grip is squeezed
    Mat attach{};
    V3 control[2]{};                                 // the controllers' grip points ([0] right), the hero's model space
    bool haveRest = false;                           // [bow] arrow_rest_*: rest is the point the arrow passes the bow hand at (AimBow made it in this pose)
    V3 rest{};                                       // the hero's model space
    V3 lift{};                                       // [bow] arrow_rest_clear: the nock and the arrow are lifted by this to clear the arm (model space; 0 mostly)
    // for the feeder's anchors, which come in the hands' frame: the eye, model units per metre, the facing
    V3 head{};
    float upm = 1.0f, yawCos = 1.0f, yawSin = 0.0f;
    float offset[3] = {};                            // Input::handOffset, added to the hands
};
BowHands g_bowHands;

// The string hand's own trigger and squeeze: written by the draw's wire each frame, before the draw takes the controls from the layout.
ULONGLONG g_bowCtlAt = 0;
int g_bowCtlSide = -1;
float g_bowCtlTrigger = 0.0f, g_bowCtlSqueeze = 0.0f;

bool BowHandRaw(int side, float* trigger, float* squeeze) {
    AcquireSRWLockShared(&g_bowLock);
    const bool fresh = g_bowCtlAt && GetTickCount64() - g_bowCtlAt < 150 && g_bowCtlSide == side;
    if (fresh) { *trigger = g_bowCtlTrigger; *squeeze = g_bowCtlSqueeze; }
    ReleaseSRWLockShared(&g_bowLock);
    return fresh;
}

int BowFeedSide(bool* drawing) {
    AcquireSRWLockShared(&g_bowLock);
    const bool fresh = g_bowFeedAt && GetTickCount64() - g_bowFeedAt < 150;
    const int side = fresh && (g_bowFeed.stringHand == 0 || g_bowFeed.stringHand == 1) ? g_bowFeed.stringHand : -1;
    *drawing = fresh && g_bowFeed.drawing;
    ReleaseSRWLockShared(&g_bowLock);
    return side;
}

void NoteBowHands(uintptr_t self, const Hero& h, const Input& in, const std::vector<Mat>& oldW, const std::vector<Mat>& newW, float c, float s, const StringAnchor& anchor) {
    BowHands b;
    b.at = GetTickCount64();
    b.hero = self;
    if (h.head >= 0 && h.weapon[0] >= 0 && h.weapon[1] >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
        // The wrist the game hung the bow on is the one whose attach bone it shows (the other it shrinks to nothing) - and when it
        // shows both (an Amazon's quiver hangs on the other), the hand the bow is held in. Only the bones were asked before, and an
        // Amazon's bow was never placed: the string's place "not read", the rig drawn along the bow instead of to the hand.
        float size[2];
        const int bowSide = BowWristNow(h, in, oldW, size).side;
        if (bowSide >= 0) {
            const int stringSide = bowSide ^ 1;
            b.placed = true;
            b.bowSide = bowSide;
            b.attach = newW[h.weapon[BowBoneSide(h, in, oldW, bowSide)]];
            b.head = in.haveEye ? V3{in.eyeModel[0], in.eyeModel[1], in.eyeModel[2]} : Pos(newW[h.head]);
            b.upm = std::clamp(b.head.y / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;
            b.yawCos = c;
            b.yawSin = s;
            memcpy(b.offset, in.handOffset, sizeof b.offset);
            if ((in.handsValid & 3u) == 3u) {
                b.hands = true;
                for (int side = 0; side < 2; ++side) b.control[side] = ControllerPoint(h, side, in, newW, c, s);
                if (anchor.ok && anchor.side == stringSide) b.control[stringSide] = anchor.point;   // [bow] string_anchor: the nock at the fingers' first joints
            }
            if (in.bowRest) {   // the rest point the hero's pose has just made (AimBow, above in this pose): the arrow's line goes through it
                AcquireSRWLockShared(&g_bowAimLock);
                if (g_restAt && b.at - g_restAt < 150) {
                    b.haveRest = true;
                    b.rest = {g_restModel[0], g_restModel[1], g_restModel[2]};
                    b.lift = {g_restLift[0], g_restLift[1], g_restLift[2]};
                }
                ReleaseSRWLockShared(&g_bowAimLock);
            }
            if (in.bowRigGrip && (in.handsValid & (1u << stringSide)) && in.grip[stringSide] > 0.6f) {
                b.gripPull = true;
                g_bowDrawnAt.store(b.at);
            }
        }
    }
    AcquireSRWLockExclusive(&g_bowLock);
    g_bowHands = b;
    ReleaseSRWLockExclusive(&g_bowLock);
}

// A bow skeleton met: the rig learned from its bind pose, or why it was not.
struct BowRigEntry {
    bowrig::Rig rig;
    float arrowAhead = 0.0f;                         // [bow] arrow_fx: the arrow mesh's far end ahead of the nock, cog frame (bowrig::MeshAhead; 0 = not read)
    std::string arrowMesh;                           // ... and the mesh it was read off, for the log
    uintptr_t bones = 0;
    bool isBow = false;                              // a held bow's model, whatever its rig
    const char* why = nullptr;                       // set when rig is not usable
    uint32_t entity = 0;
    bool entityKnown = false;
    ULONGLONG lookupAt = 0;
    // for the log (BowRigWho): which skeleton this is, whose, and what its last pose did - every bow skeleton the game poses has one,
    // the hero's own among them, and the statuses of the others must not stand for his
    std::string model;                               // the model's file name
    int count = 0;                                   // its bones
    bowcensus::Owner owner = bowcensus::Owner::Unknown;
    uint32_t parent = 0;                             // the entity it hangs off
    ULONGLONG ownerAt = 0;                           // when the owner was last looked up (an unknown rig is asked twice a second at most)
    ULONGLONG seenAt = 0;
    const char* last = "not posed yet";
    // the last draw, in the cog's frame, and when: a cancelled pull goes back to rest from there
    bowrig::V3 drawnNock, drawnDir;
    float drawnPeak = 1.0f;                          // the most the feed's amount was in this pull
    bool pulling = false;
    ULONGLONG drawnAt = 0;
};
SRWLOCK g_bowRigLock = SRWLOCK_INIT;
// What the game's own pose does with the arrow bone, against its bind size (read from every pose we get, before ours
// is written): the lowest and the highest, and how many poses - for vrcam's log. The arrow is part of the bow's mesh and
// the game shows and hides it by this bone's size; the numbers say what "shown" is on this install.
std::atomic<float> g_bowArrowLow{1e9f}, g_bowArrowHigh{0.0f};
std::atomic<uint32_t> g_bowArrowSamples{0};
std::map<std::pair<uintptr_t, uintptr_t>, BowRigEntry> g_bowRigs;
// One line for vrcam's log for each bow skeleton learned or refused: which model, which layout (or its bones, if unknown).
SRWLOCK g_bowNoteLock = SRWLOCK_INIT;
std::string g_bowNote;

void NoteBow(const std::string& line) {
    AcquireSRWLockExclusive(&g_bowNoteLock);
    if (g_bowNote.size() < 1500) g_bowNote += (g_bowNote.empty() ? "" : " | ") + line;
    ReleaseSRWLockExclusive(&g_bowNoteLock);
}

// What this bow skeleton is: its bones read, matched to a layout and learned from its bind pose.
void LearnBow(uintptr_t self, uintptr_t bones, int count, BowRigEntry* e) {
    uintptr_t object = 0, name = 0;
    Get(self, &object); Get(object + 8, &name);
    const std::string path = Str(name);
    e->isBow = path.find("/items/weapon/bow/") != std::string::npos && path.find("dropped") == std::string::npos;
    if (!e->isBow) return;
    const std::string model = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    e->model = model;
    e->count = count;
    std::string names[bowrig::kMaxBones];
    std::string_view views[bowrig::kMaxBones];
    int parents[bowrig::kMaxBones] = {};
    bool read = count >= 1 && count <= bowrig::kMaxBones;
    for (int i = 0; i < count && read; ++i) {
        uintptr_t nm = 0;
        const uintptr_t at = bones + (uintptr_t)i * 0xA4;
        read = Get(at, &nm) && Get(at + 8, &parents[i]);
        names[i] = read ? Str(nm) : std::string();
        views[i] = names[i];
    }
    auto bonesList = [&] { std::string s; for (int i = 0; i < count && i < bowrig::kMaxBones; ++i) s += (i ? " " : "") + names[i] + ">" + std::to_string(parents[i]); return s; };
    const char* why = nullptr;
    bowrig::Resolved resolved;
    if (!read) {
        e->why = "a bow whose bones do not read";
    } else if (!bowrig::Resolve(views, parents, count, &resolved, &why)) {
        e->why = "a bow that is not a known rig - the game's animation";
        NoteBow("bow rig: " + model + " is not a known layout (" + std::string(why ? why : "") + "), bones " + std::to_string(count) + ": " + bonesList());
    } else {
        bowrig::Mat bind[bowrig::kMaxBones];
        for (int i = 0; i < count && read; ++i) {   // the bind pose: the inverse of the world matrix stored at +0x50
            Mat inverse;
            read = SafeCopy(inverse.m, (const void*)(bones + (uintptr_t)i * 0xA4 + 0x50), sizeof inverse.m);
            if (read) { const Mat world = InvAffine(inverse); memcpy(bind[i].m, world.m, sizeof world.m); }
        }
        const char* learnWhy = nullptr;
        if (!read || !e->rig.Learn(resolved, bind, count, &learnWhy)) {
            e->why = "a known layout, but its bind pose did not read as a bow";
            NoteBow("bow rig: " + model + " matched '" + resolved.layout->name + "' but its bind pose was refused: " + (learnWhy ? learnWhy : "unreadable"));
        } else {
            // [bow] arrow_fx: the arrowhead off the arrow mesh's own box (granny_mesh.h), read once with the rig
            uintptr_t instance = 0, skel = 0;
            grannymesh::Box box;
            auto rd = [](uintptr_t at, void* to, size_t n) { return SafeCopy(to, (const void*)at, n); };
            if (Get(self + 0x08, &instance) && Get(instance + 0x08, &skel) && grannymesh::ArrowBox(rd, instance, skel, &box)) {
                e->arrowAhead = bowrig::MeshAhead(e->rig, bind[resolved.cog], box.min, box.max);
                e->arrowMesh = box.mesh;
            }
            char arrow[200];
            if (e->arrowAhead > 0.0f) snprintf(arrow, sizeof arrow, "; arrow mesh %s, its far end %.2f ahead of the nock (the riser %.2f)", e->arrowMesh.c_str(), e->arrowAhead, e->rig.reach);
            else snprintf(arrow, sizeof arrow, "; the arrow mesh not read");
            NoteBow("bow rig: " + model + " is '" + resolved.layout->name + "', " + std::to_string(count) + " bones; limbs " + names[resolved.bend[0]] + " / " +
                    names[resolved.bend[1]] + ", string ends " + names[resolved.tip[0]] + " / " + names[resolved.tip[1]] + ", nock " + names[resolved.nock] +
                    ", arrow " + names[resolved.arrow] + arrow);
        }
    }
}

// THE interface for any bow rig (the hunter's, and whatever layouts bow-rig-pose learns after it): once a pose of the hero's own
// bow has its three matrices - the bow's cog bone as the game's pose has it now, the bow entity's transform relative to the hand
// (itemLocal), and the wrist's attach bone as ours left it (hands.attach) - and the string's place at rest in the cog's frame
// (`restNock`: the bind pose's string bone), this puts it into the frame of the hands and publishes it for skel::BowString and the
// bow draw's touch point. Nothing else is needed from a layout, and nothing here knows which one it is.
void PublishBowString(const BowHands& hands, ULONGLONG now, const bowrig::Mat& cog, const bowrig::Mat& itemLocal, bowrig::V3 restNock) {
    if (!hands.placed || hands.hero != g_heroSelf.load() || now - hands.at >= 150 || !(hands.upm > 1e-3f)) return;
    bowrig::Mat attach;
    memcpy(attach.m, hands.attach.m, sizeof attach.m);
    const bowrig::Mat toHero = bowrig::Mul(cog, bowrig::Mul(itemLocal, attach));
    const bowrig::V3 p = bowrig::Point(toHero, restNock);
    if (!bowrig::Finite(p)) return;
    const bowdraw::Vec rest = bowtouch::ModelToHands({p.x, p.y, p.z}, {hands.offset[0], hands.offset[1], hands.offset[2]},
                                                     {hands.head.x, hands.head.y, hands.head.z}, hands.upm, hands.yawCos, hands.yawSin);
    if (!bowdraw::Finite(rest)) return;
    AcquireSRWLockExclusive(&g_bowStringLock);
    g_bowStringAt[0] = rest.x; g_bowStringAt[1] = rest.y; g_bowStringAt[2] = rest.z;
    g_bowStringStamp = now;
    ReleaseSRWLockExclusive(&g_bowStringLock);
}

// [bow] quiver_orbs: the same for the bow's stave - the riser's middle and the end of a limb at rest, and the way the archer is (the rig's `back`, from the grip toward the nock), from
// the cog's frame to the frame of the hands. The orbs sit on it and follow the bow, wherever the hand takes it. A bow has no top and bottom of its own: how it hangs in the hand
// decides which of its two limbs points up (the rig's "upper" one may well hang down), so the stave runs along the limb whose end is the HIGHER of the two in the hands' frame
// (y up) - the orbs go above the hand, not below it.
void PublishBowStave(const BowHands& hands, ULONGLONG now, const bowrig::Mat& cog, const bowrig::Mat& itemLocal, bowrig::V3 grip, bowrig::V3 tipA, bowrig::V3 tipB, bowrig::V3 back) {
    if (!hands.placed || hands.hero != g_heroSelf.load() || now - hands.at >= 150 || !(hands.upm > 1e-3f)) return;
    bowrig::Mat attach;
    memcpy(attach.m, hands.attach.m, sizeof attach.m);
    const bowrig::Mat toHero = bowrig::Mul(cog, bowrig::Mul(itemLocal, attach));
    const float len = bowrig::Length(tipA - grip);
    if (!(len > 1e-6f) || !bowrig::Finite(grip) || !bowrig::Finite(tipA) || !bowrig::Finite(tipB) || !bowrig::Finite(back)) return;
    auto toHands = [&](bowrig::V3 v, bowdraw::Vec* out) {
        const bowrig::V3 p = bowrig::Point(toHero, v);
        if (!bowrig::Finite(p)) return false;
        *out = bowtouch::ModelToHands({p.x, p.y, p.z}, {hands.offset[0], hands.offset[1], hands.offset[2]}, {hands.head.x, hands.head.y, hands.head.z}, hands.upm, hands.yawCos, hands.yawSin);
        return bowdraw::Finite(*out);
    };
    bowdraw::Vec g, tA, tB, b;
    if (!toHands(grip, &g) || !toHands(tipA, &tA) || !toHands(tipB, &tB) || !toHands(grip + bowrig::Unit(back) * (len * 0.2f), &b)) return;
    static int tipWas = -1;   // (the pose hook's one thread)
    tipWas = bowtouch::HigherEnd(tA, tB, tipWas);
    const bowdraw::Vec t = tipWas == 0 ? tA : tB;   // the limb that points up
    const bowdraw::Vec d = bowdraw::Normalized(b - g);
    if (!(bowdraw::Length(d) > 0.5f)) return;
    AcquireSRWLockExclusive(&g_bowStaveLock);
    g_bowStaveAt[0] = g.x; g_bowStaveAt[1] = g.y; g_bowStaveAt[2] = g.z;
    g_bowStaveAt[3] = t.x; g_bowStaveAt[4] = t.y; g_bowStaveAt[5] = t.z;
    g_bowStaveAt[6] = d.x; g_bowStaveAt[7] = d.y; g_bowStaveAt[8] = d.z;
    g_bowStaveStamp = now;
    ReleaseSRWLockExclusive(&g_bowStaveLock);
}

// [bow] arrow_fx: the bow's own arrow as `arrowSkel` (the arrow bone's matrix as the pose leaves it, the bow's skeleton space) draws it, in the frame of the hands.
// The arrow's two ends in the bind pose (cog frame): the nock (the string's rest place) and the head - the farthest of the arrow's own bones along the arrow (the
// rig's -back), or, when they do not reach past the riser, the riser itself (the arrow passes the bow there). Both are carried by the arrow bone from its bind
// place to where the pose has it, then to the hero and the hands as the string's place is. A hidden arrow (its bone at a thousandth: let go) is not published.
void PublishBowArrow(const BowHands& hands, ULONGLONG now, const bowrig::Rig& rig, float meshAhead, const std::string& meshName, const bowrig::Mat& cog, const bowrig::Mat& itemLocal,
                     const bowrig::Mat& arrowSkel, bool drawn) {
    if (!hands.placed || hands.hero != g_heroSelf.load() || now - hands.at >= 150 || !(hands.upm > 1e-3f)) return;
    const bowrig::Resolved& R = rig.r;
    bowrig::ArrowEnds ends;
    if (!bowrig::ArrowEndsOf(rig, &ends, meshAhead)) return;
    const bool fromMesh = ends.fromMesh, fromBones = ends.fromBones;
    const int farthest = ends.bone;
    const bowrig::V3 nockBind = ends.nock, headBind = ends.head;
    // the arrow bone's size against its bind size: a hidden arrow (kArrowHidden) is not drawn
    const bowrig::Mat bind = bowrig::Mul(rig.rest[R.arrow], cog);
    const float bindSize = bowrig::Length({bind.m[0], bind.m[1], bind.m[2]}), size = bowrig::Length({arrowSkel.m[0], arrowSkel.m[1], arrowSkel.m[2]});
    if (!(bindSize > 1e-6f) || !(size > 0.5f * bindSize)) return;
    bowrig::Mat toLocal;
    if (!bowrig::Inverse(rig.rest[R.arrow], &toLocal)) return;
    bowrig::Mat attach;
    memcpy(attach.m, hands.attach.m, sizeof attach.m);
    const bowrig::Mat toHero = bowrig::Mul(itemLocal, attach);
    auto toHands = [&](bowrig::V3 v, bowdraw::Vec* out) {
        const bowrig::V3 local = bowrig::Point(toLocal, v), skel = bowrig::Point(arrowSkel, local), p = bowrig::Point(toHero, skel);
        if (!bowrig::Finite(p)) return false;
        *out = bowtouch::ModelToHands({p.x, p.y, p.z}, {hands.offset[0], hands.offset[1], hands.offset[2]}, {hands.head.x, hands.head.y, hands.head.z}, hands.upm, hands.yawCos, hands.yawSin);
        return bowdraw::Finite(*out);
    };
    bowdraw::Vec n, h;
    if (!toHands(nockBind, &n) || !toHands(headBind, &h) || !(bowdraw::Length(h - n) > 0.02f)) return;
    char who[260];
    if (fromMesh) snprintf(who, sizeof who, "the head at the arrow mesh's far end (%s), %.0f cm ahead of the nock", meshName.c_str(), bowdraw::Length(h - n) * 100.0f);
    else snprintf(who, sizeof who, fromBones ? "the arrow mesh not read; its own bones reach %.0f cm ahead of the nock (bone %d)"
                                             : "the arrow mesh not read and its own bones do not reach past the riser: the head at the riser, %.0f cm ahead of the nock (bone %d the farthest)",
                  bowdraw::Length(h - n) * 100.0f, farthest);
    AcquireSRWLockExclusive(&g_bowArrowLock);
    g_bowArrowAt[0] = n.x; g_bowArrowAt[1] = n.y; g_bowArrowAt[2] = n.z;
    g_bowArrowAt[3] = h.x; g_bowArrowAt[4] = h.y; g_bowArrowAt[5] = h.z;
    g_bowArrowDrawn = drawn;
    g_bowArrowStamp = now;
    if (g_bowArrowWho.empty() || g_bowArrowWho.compare(0, 20, who, 20) != 0) g_bowArrowWho = who;
    ReleaseSRWLockExclusive(&g_bowArrowLock);
}

// A held bow's skeleton that is NOT the hunter's 13-bone rig (the Hydra Bow, for one): said once per skeleton, so the log tells
// which layouts exist - the path and the bone count are what bow-rig-pose needs to learn them. Read only while the bow draw wants
// the string (it is the debugging case), and each skeleton instance once.
std::atomic<uintptr_t> g_bowProbeSeen[1024];
SRWLOCK g_bowProbeLock = SRWLOCK_INIT;
std::string g_bowProbeText;

void PoseBowRig(uintptr_t self) {
    const bool rigOn = g_bowRigOn.load(std::memory_order_relaxed), stringOn = g_bowStringOn.load(std::memory_order_relaxed), aimOn = g_bowAimOn.load(std::memory_order_relaxed),
               staveOn = g_bowStaveOn.load(std::memory_order_relaxed), arrowOn = g_bowArrowOn.load(std::memory_order_relaxed);
    if (!rigOn && !stringOn && !aimOn && !staveOn && !arrowOn) return;
    const ULONGLONG now = GetTickCount64();
    // the common case: the string is not drawn (and nobody wants to know where it rests or which way the bow points): nothing is looked at
    const bool holding = rigOn && bowrig::Holding(g_bowDrawnAt.load(), now);
    if (!holding && !stringOn && !aimOn && !staveOn && !arrowOn) return;
    BowDraw feed;
    BowHands hands;
    ULONGLONG feedAt;
    AcquireSRWLockShared(&g_bowLock);
    feed = g_bowFeed; feedAt = g_bowFeedAt; hands = g_bowHands;
    ReleaseSRWLockShared(&g_bowLock);
    const bool fed = feedAt && now - feedAt < 150;
    const bool drawing = (fed && feed.drawing) || (hands.gripPull && now - hands.at < 150);
    const float arrowScale = !fed || feed.arrowVisible ? 1.0f : bowrig::kArrowHidden;   // while drawn; let go it is hidden

    // Is this skeleton a bow's rig we know - and the hero's?
    uintptr_t model = 0, skeleton = 0, bones = 0;
    int count = 0;
    if (!Get(self + 0x08, &model) || !Get(model + 0x08, &skeleton) || !Get(skeleton + 8, &count) || count < 8 || count > bowrig::kMaxBones ||
        !Get(skeleton + 0xC, &bones) || !bones) return;   // anything but a skeleton the size of a bow's leaves here
    const auto key = std::make_pair(self, skeleton);
    if (KnownNot(g_bowRigLock, g_bowRigs, key, bones, [](const BowRigEntry& e) { return !e.isBow; })) return;   // most rigs: not a bow, told under the shared lock
    AcquireSRWLockExclusive(&g_bowRigLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_bowRigLock); } } unlock;
    auto it = g_bowRigs.find(key);
    if (it == g_bowRigs.end() || it->second.bones != bones) {
        BowRigEntry e;
        e.bones = bones;
        LearnBow(self, bones, count, &e);
        if (g_bowRigs.size() > 4096) std::erase_if(g_bowRigs, [](const auto& b) { return !b.second.isBow; });   // full: the bows are kept
        if (g_bowRigs.size() > 4096) g_bowRigs.clear();
        it = g_bowRigs.insert_or_assign(key, e).first;
    }
    BowRigEntry& held = it->second;
    if (!held.isBow) return;
    held.seenAt = now;
    // Every status below is this skeleton's own (BowRigWho); only the HERO's bow speaks for BowRigStatus. The game keeps others alive -
    // his other weapon set's crossbow, a preview of his gear, another player's bow - and when they all wrote one shared reason the log
    // flipped between theirs ("not a known rig" / "not the hero's") every few milliseconds and never said what his own bow did.
    HeroXform hero;
    if (!HeroTransform(&hero)) { held.last = "the hero's transform is not known"; g_bowWhy.store(held.last); return; }
    bowrig::Mat itemLocal;
    // Whose is it? Only the hero's own bow is posed: its entity hangs off his (an NPC's or a dropped bow of the same model never does).
    // A rig we do not know is asked too (to tell his crossbow from another's), twice a second at most.
    if (held.rig.valid || !held.ownerAt || now - held.ownerAt >= 500) {
        AcquireSRWLockExclusive(&ecs::g_lock);
        struct EcsUnlock { ~EcsUnlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } ecsUnlock;
        uintptr_t existing = 0;
        bool found = held.entityKnown && ecs::SkeletonOf(held.entity, &existing) && existing == self;
        if (!found) {
            held.entityKnown = false;
            if (!held.lookupAt || now - held.lookupAt >= 500) {   // looked for twice a second at most
                held.lookupAt = now;
                found = ecs::EntityOf(self, &held.entity);
                held.entityKnown = found;
            }
        }
        if (!found) { held.last = "its entity is not found yet"; return; }
        uint32_t index = 0;
        uintptr_t components = 0;
        if (!ecs::Dense(ecs::g_xs, held.entity, &index) || !Get(ecs::g_xs + 0x38, &components) || !components ||
            !Get(components + (uintptr_t)index * kTransformSize + 0xC0, &held.parent)) {
            held.last = "its entity's transform is not found";
            return;
        }
        held.ownerAt = now;
        held.owner = (held.parent & 0xFFFFF) == (hero.entity & 0xFFFFF) ? bowcensus::Owner::Hero : bowcensus::Owner::Other;
        if (held.owner == bowcensus::Owner::Hero && held.rig.valid &&
            !SafeCopy(itemLocal.m, (const void*)(components + (uintptr_t)index * kTransformSize), sizeof itemLocal.m)) {
            held.last = "its entity's transform did not read";
            return;
        }
    }
    if (held.owner != bowcensus::Owner::Hero) { held.last = "not the hero's"; return; }
    if (!held.rig.valid) { held.last = held.why ? held.why : "a rig that is not known"; return; }   // his, but a rig we do not know (his crossbow): the game's animation
    const bowrig::Resolved& R = held.rig.r;
    uintptr_t world = 0, matrices = 0;
    int worldCount = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &worldCount) || worldCount != R.count || !Get(world + 4, &matrices) || !matrices) {
        held.last = "its pose matrices are not the rig's";
        return;
    }
    bowrig::Pose native, posed;
    if (!SafeCopy(native.data(), (const void*)matrices, sizeof(Mat) * (size_t)R.count)) { held.last = "its pose matrices did not read"; return; }
    posed = native;   // what is not the rig's stays the game's
    const bowrig::Mat& cog = native[R.cog];
    if (aimOn) {   // [bow] aim_along_hands: the bow's forward axis (from the nock toward the grip) and its limbs' axis in its attach bone's frame, for the hero's pose
        const bowrig::Mat toAttach = bowrig::Mul(cog, itemLocal);
        const bowrig::V3 f = bowrig::Unit(bowrig::Direction(toAttach, held.rig.back * -1.0f)), u = bowrig::Unit(bowrig::Direction(toAttach, held.rig.up));
        // the third axis: up x forward in the bow's own frame, carried the same way (so that it is the model's own, whatever the chain mirrors)
        const bowrig::V3 sd = bowrig::Unit(bowrig::Direction(toAttach, bowrig::Unit(bowrig::Cross(held.rig.up, held.rig.back * -1.0f))));
        if (bowrig::Finite(f) && bowrig::Finite(u) && bowrig::Finite(sd) && bowrig::Length(f) > 0.5f && bowrig::Length(u) > 0.5f && bowrig::Length(sd) > 0.5f) {
            AcquireSRWLockExclusive(&g_bowAimLock);
            g_bowAxF[0] = f.x; g_bowAxF[1] = f.y; g_bowAxF[2] = f.z;
            g_bowAxU[0] = u.x; g_bowAxU[1] = u.y; g_bowAxU[2] = u.z;
            g_bowAxS[0] = sd.x; g_bowAxS[1] = sd.y; g_bowAxS[2] = sd.z;
            g_bowAxesAt = now;
            ReleaseSRWLockExclusive(&g_bowAimLock);
        }
    }
    {   // [bow] arrow_rest_model: the bow's own arrow rest, into its attach bone's frame (the hero's pose carries it from there: AimBow)
        bowrig::V3 r;
        if (bowrig::ArrowRestPoint(held.rig, &r)) {
            const bowrig::V3 p = bowrig::Point(bowrig::Mul(cog, itemLocal), r);
            if (bowrig::Finite(p)) {
                AcquireSRWLockExclusive(&g_bowAimLock);
                g_restAttach[0] = p.x; g_restAttach[1] = p.y; g_restAttach[2] = p.z;
                g_restAttachAt = now;
                ReleaseSRWLockExclusive(&g_bowAimLock);
            }
        }
    }
    if (staveOn) {   // [bow] quiver_orbs: the stave, from the rig's bind pose (the riser = the middle of the limbs' hinges, the upper limb's end = where the string is tied to it)
        const bowrig::V3 grip = (bowrig::Position(held.rig.rest[R.bend[0]]) + bowrig::Position(held.rig.rest[R.bend[1]])) * 0.5f;
        PublishBowStave(hands, now, cog, itemLocal, grip, bowrig::Position(held.rig.rest[R.tip[0]]), bowrig::Position(held.rig.rest[R.tip[1]]), held.rig.back);
    }
    if (arrowOn && !holding) PublishBowArrow(hands, now, held.rig, held.arrowAhead, held.arrowMesh, cog, itemLocal, native[R.arrow], false);   // [bow] arrow_fx: the arrow as the game's own pose has it (nocked at rest)
    if (!holding && !stringOn) { held.last = "its axes are read; the string is not drawn"; return; }   // only the bow's axes were wanted
    {   // the arrow bone's size in the game's pose against the bind pose's (rows of the matrices)
        const bowrig::Mat bind = bowrig::Mul(held.rig.rest[R.arrow], cog);
        const float was = bowrig::Length({bind.m[0], bind.m[1], bind.m[2]}), now2 = bowrig::Length({native[R.arrow].m[0], native[R.arrow].m[1], native[R.arrow].m[2]});
        if (was > 1e-6f && std::isfinite(now2)) {
            const float k = now2 / was;
            if (k < g_bowArrowLow.load()) g_bowArrowLow.store(k);
            if (k > g_bowArrowHigh.load()) g_bowArrowHigh.store(k);
            g_bowArrowSamples.fetch_add(1);
        }
    }

    // [hands] bow_draw: where this bow's string rests as it hangs in the hero's hand - the rig's bind-pose nock, from the bow's
    // cog frame to the hero's model space, and from there to the frame of the hands (the arms' own mapping run backwards).
    if (stringOn) PublishBowString(hands, now, cog, itemLocal, held.rig.restNock);
    if (!holding) { held.last = "its string place is read; the string is not drawn"; return; }   // only the string's place was wanted

    // The string hand and the aim, in the cog's frame: the feeder's own anchors (in the hands' frame) if it
    // gave them, else the controllers; with neither, a plain pull by its amount along the bow.
    bool atHands = false;
    bowrig::V3 nock, line;
    if (drawing && hands.placed && hands.hero == g_heroSelf.load() && now - hands.at < 150) {
        const int pulls = fed && (feed.stringHand == 0 || feed.stringHand == 1) ? feed.stringHand : hands.bowSide ^ 1;
        // The arrow lies on the line from the string hand through the bow hand - and through the bow hand's reference point it ran through
        // the palm (and the place it passed at slid with the hands). [bow] arrow_rest_*: through the rest point instead, a point fixed in
        // the bow hand's own frame, which the hero's pose made (AimBow) in this very pose; the nock stays at the string hand.
        V3 stringHand = hands.control[pulls], bowHand = hands.haveRest ? hands.rest : hands.control[pulls ^ 1];
        bool have = hands.hands;
        if (fed && feed.anchored) {
            const float nockAt[3] = {feed.nock[0] + hands.offset[0], feed.nock[1] + hands.offset[1], feed.nock[2] + hands.offset[2]};
            stringHand = Add(hands.head, Scale(ToModel(nockAt, hands.yawCos, hands.yawSin), hands.upm));
            bowHand = Add(stringHand, ToModel(feed.dir, hands.yawCos, hands.yawSin));   // only the way it points is used
            have = true;
        }
        bowrig::Mat attach;
        memcpy(attach.m, hands.attach.m, sizeof attach.m);
        if (have && bowrig::ToRigFrame(cog, itemLocal, attach, {stringHand.x, stringHand.y, stringHand.z},
                                       {bowHand.x, bowHand.y, bowHand.z}, &nock, &line)) {
            // [bow] arrow_rest_clear: where the arrow would pass through the hand or the forearm it is lifted off it, the nock with it, parallel to
            // the line (the hero's pose worked the lift out from the arm as it left it); a nock the limbs could not then meet the string at - the
            // very end of a draw - stays where it was, no jump.
            const V3 lift = hands.haveRest ? hands.lift : V3{0.0f, 0.0f, 0.0f};
            if (lift.x != 0.0f || lift.y != 0.0f || lift.z != 0.0f) {
                bowrig::V3 nockLifted, lineLifted;
                if (bowrig::ToRigFrame(cog, itemLocal, attach, {stringHand.x + lift.x, stringHand.y + lift.y, stringHand.z + lift.z},
                                       {bowHand.x + lift.x, bowHand.y + lift.y, bowHand.z + lift.z}, &nockLifted, &lineLifted) && held.rig.CanReach(nockLifted)) {
                    nock = nockLifted;
                    line = lineLifted;
                }
            }
            atHands = held.rig.DrawAt(cog, nock, line, arrowScale, &posed);
        }
    }
    bool posedOk = atHands;
    const char* how = atHands ? "posed - drawn to the hands" : "posed - drawn by the feed's amount only (the hands are not placed)";
    if (!drawing) held.pulling = false;   // the next pull starts a new peak
    if (drawing) {
        if (!posedOk) {
            nock = held.rig.NockAt(fed ? feed.draw : 0.0f);
            line = held.rig.back * -1.0f;
            posedOk = held.rig.Draw(cog, fed ? feed.draw : 0.0f, arrowScale, &posed);
        }
        if (posedOk) {
            held.drawnNock = nock; held.drawnDir = line; held.drawnAt = now;
            const float amount = fed ? feed.draw : 1.0f;
            held.drawnPeak = held.pulling ? std::max(held.drawnPeak, amount) : amount;
            held.pulling = true;
        }
    } else if (fed && (feed.cancelled || bowrig::ShortPull(held.drawnPeak))) {
        // a pull that came to nothing (the feed says so, or it was too short to shoot): the string goes back to rest over
        // ~0.13 s, the arrow stays on it, and then the game's own animation is left alone - no attack was started, so there
        // is no wind-up to hold off
        const ULONGLONG since = now - held.drawnAt;
        if (!held.drawnAt || since >= bowrig::kCancelMs) { held.last = "a cancelled pull is back at rest: the game's animation"; return; }
        posedOk = held.rig.DrawAt(cog, bowrig::EasedNock(held.drawnNock, held.rig.restNock, since), bowrig::EasedDirection(held.drawnDir, held.rig.back * -1.0f, since), 1.0f, &posed);
        how = "posed - a cancelled pull going back to rest";
    } else {
        // let go: at rest, the arrow hidden, the release point at the grip (the game's shot starts from it)
        posedOk = held.rig.Released(cog, &posed);
        how = "posed - held at rest after the release";
    }
    if (!posedOk) {
        held.last = "the pose did not come out finite";
        g_bowWhy.store(held.last);
        return;
    }
    // The game's root, translate, anim and cog stay; the rig's bones are replaced (and the ones between them that are not stay
    // the game's). Held after a shot's release too (at rest, the arrow hidden, its release point at the grip): the game's own
    // draw, started by the shot, would play late otherwise, and the game's own arrow leaves from that point.
    if (SafeWrite((void*)(matrices + (size_t)R.lowest * sizeof(Mat)), posed.data() + R.lowest, (size_t)(R.highest - R.lowest + 1) * sizeof(Mat))) {
        g_bowPosedAt.store(now);
        held.last = how;
        if (arrowOn) PublishBowArrow(hands, now, held.rig, held.arrowAhead, held.arrowMesh, cog, itemLocal, posed[R.arrow], drawing);   // [bow] arrow_fx: the arrow as our pose draws it
    } else {
        held.last = "the pose could not be written";
    }
}

// [hands] xbow_support_model / [debug] xbow_barrel_line: the barrel of the crossbow in the hero's hand, read off its own skeleton -
// the bolt_helper bone's line through the front of the stock (between L_bowBend and R_bowBend), from the bind pose (xbowgrip::
// BarrelInCog), carried by the cog bone as the game poses it now and by the item's transform relative to its attach bone (the frame
// [bow] aim_along_hands reads a bow's axes in): published in the attach bone's frame for PoseBody, which puts it on the attach bone as
// ours placed it. Only the hero's own crossbow (its entity hangs off his); read only while asked for. Nothing of it is written.
struct XbowRigEntry {
    uintptr_t bones = 0;
    bool ok = false;                 // a crossbow whose barrel was read
    int cog = -1, count = 0;
    xbowgrip::M barrelInCog;
    std::string model;
    uint32_t entity = 0;
    bool entityKnown = false, hero = false;
    ULONGLONG lookupAt = 0, ownerAt = 0;
    // [debug] xbow_barrel_line: the crossbow's own loaded bolt (its "bolt" bone, the model's, not a missile): its bind place in the cog's
    // frame and the limbs' span (for scale), to tell in the log when the game's own animation moves it off the crossbow
    int bolt = -1;
    xbowgrip::V boltBindInCog;
    float span = 0.0f;
    bool boltAway = false;
    ULONGLONG boltToldAt = 0;
};
std::atomic<bool> g_xbowBoltDiag{false};   // [debug] xbow_barrel_line: the loaded bolt's line in the log (Set)
SRWLOCK g_xbowRigLock = SRWLOCK_INIT;
std::map<std::pair<uintptr_t, uintptr_t>, XbowRigEntry> g_xbowRigs;

void LearnCrossbow(uintptr_t self, uintptr_t bones, int count, XbowRigEntry* e) {
    uintptr_t object = 0, name = 0;
    Get(self, &object); Get(object + 8, &name);
    const std::string path = Str(name);
    if (path.find("/items/weapon/") == std::string::npos || path.find("dropped") != std::string::npos) return;
    int cog = -1, bolt = -1, left = -1, right = -1, trigger = -1;
    for (int i = 0; i < count; ++i) {
        uintptr_t nm = 0;
        if (!Get(bones + (uintptr_t)i * 0xA4, &nm)) return;
        const std::string bone = Str(nm);
        if (bone == "cog") cog = i;
        else if (bone == "bolt_helper") bolt = i;
        else if (bone == "L_bowBend") left = i;
        else if (bone == "R_bowBend") right = i;
        else if (bone == "bolt") e->bolt = i;
        else if (bone == "trigger") trigger = i;   // hangs below the stock: which way is down
    }
    if (bolt < 0) return;   // not a crossbow (a bow has an arrow_helper)
    e->model = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    e->count = count;
    auto inverse = [&](int i, xbowgrip::M* m) { return i >= 0 && SafeCopy(m->m, (const void*)(bones + (uintptr_t)i * 0xA4 + 0x50), sizeof m->m); };
    xbowgrip::M ic, ib, il, ir;
    if (!inverse(cog, &ic) || !inverse(bolt, &ib) || !inverse(left, &il) || !inverse(right, &ir)) {
        NoteBow("crossbow barrel: " + e->model + " has a bolt_helper but not cog / L_bowBend / R_bowBend that read - the left hand stays on the line set by eye");
        return;
    }
    xbowgrip::M it;
    const bool haveTrigger = inverse(trigger, &it);
    if (!xbowgrip::BarrelInCog(ic, ib, il, ir, &e->barrelInCog, haveTrigger ? &it : nullptr)) {
        NoteBow("crossbow barrel: " + e->model + "'s bolt_helper / limbs did not make a line - the left hand stays on the line set by eye");
        return;
    }
    e->cog = cog;
    {   // the loaded bolt's bind place in the cog's frame, and the limbs' span
        xbowgrip::M boltInverse, boltBind, cogBind, left, right;
        if (inverse(e->bolt, &boltInverse) && xbowgrip::Inverse(boltInverse, &boltBind) && xbowgrip::Inverse(ic, &cogBind) &&
            xbowgrip::Inverse(il, &left) && xbowgrip::Inverse(ir, &right)) {
            e->boltBindInCog = xbowgrip::Place(xbowgrip::Mul(boltBind, ic));
            const xbowgrip::V d = xbowgrip::Sub(xbowgrip::Place(left), xbowgrip::Place(right));
            e->span = std::sqrt(xbowgrip::Dot(d, d));
        } else {
            e->bolt = -1;
        }
    }
    e->ok = true;
    NoteBow("crossbow barrel: " + e->model + " read (" + std::to_string(count) + " bones): its bolt_helper's line through the front of the stock, up square to the limbs" +
            (haveTrigger ? ", the trigger below" : " (no trigger bone: up on the bolt_helper's +Y side)"));
}

void NoteCrossbowBarrel(uintptr_t self) {
    if (!g_xbowBarrelOn.load(std::memory_order_relaxed) || self == g_heroSelf.load(std::memory_order_relaxed)) return;
    uintptr_t model = 0, skeleton = 0, bones = 0;
    int count = 0;
    if (!Get(self + 0x08, &model) || !Get(model + 0x08, &skeleton) || !Get(skeleton + 8, &count) || count < 4 || count > 64 ||
        !Get(skeleton + 0xC, &bones) || !bones) return;
    const ULONGLONG now = GetTickCount64();
    const auto key = std::make_pair(self, skeleton);
    if (KnownNot(g_xbowRigLock, g_xbowRigs, key, bones, [](const XbowRigEntry& e) { return !e.ok; })) return;   // most rigs: not a crossbow that read, told under the shared lock
    AcquireSRWLockExclusive(&g_xbowRigLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_xbowRigLock); } } unlock;
    auto it = g_xbowRigs.find(key);
    if (it == g_xbowRigs.end() || it->second.bones != bones) {
        XbowRigEntry e;
        e.bones = bones;
        LearnCrossbow(self, bones, count, &e);
        if (g_xbowRigs.size() > 4096) std::erase_if(g_xbowRigs, [](const auto& x) { return !x.second.ok; });   // full: the crossbows are kept
        if (g_xbowRigs.size() > 4096) g_xbowRigs.clear();
        it = g_xbowRigs.insert_or_assign(key, e).first;
    }
    XbowRigEntry& held = it->second;
    if (!held.ok) return;
    HeroXform hero;
    if (!HeroTransform(&hero)) { g_xbowBarrelWhy.store("the hero's transform is not known"); return; }
    xbowgrip::M itemLocal;
    {   // whose: its entity hangs off the hero's (another player's, a preview's or a dropped one never does); looked up twice a second at most
        AcquireSRWLockExclusive(&ecs::g_lock);
        struct EcsUnlock { ~EcsUnlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } ecsUnlock;
        uintptr_t existing = 0;
        bool found = held.entityKnown && ecs::SkeletonOf(held.entity, &existing) && existing == self;
        if (!found) {
            held.entityKnown = false;
            if (held.lookupAt && now - held.lookupAt < 500) return;
            held.lookupAt = now;
            found = held.entityKnown = ecs::EntityOf(self, &held.entity);
            if (!found) { g_xbowBarrelWhy.store("the crossbow's entity is not found yet"); return; }
        }
        uint32_t index = 0, parent = 0;
        uintptr_t components = 0;
        if (!ecs::Dense(ecs::g_xs, held.entity, &index) || !Get(ecs::g_xs + 0x38, &components) || !components ||
            !Get(components + (uintptr_t)index * kTransformSize + 0xC0, &parent)) {
            g_xbowBarrelWhy.store("the crossbow entity's transform is not found");
            return;
        }
        if ((parent & 0xFFFFF) != (hero.entity & 0xFFFFF)) return;   // not the hero's: says nothing (his own speaks)
        if (!SafeCopy(itemLocal.m, (const void*)(components + (uintptr_t)index * kTransformSize), sizeof itemLocal.m)) {
            g_xbowBarrelWhy.store("the crossbow entity's transform did not read");
            return;
        }
    }
    uintptr_t world = 0, matrices = 0;
    int worldCount = 0;
    xbowgrip::M cog;
    if (!Get(self + 0x20, &world) || !Get(world, &worldCount) || worldCount != held.count || !Get(world + 4, &matrices) || !matrices ||
        !SafeCopy(cog.m, (const void*)(matrices + (uintptr_t)held.cog * sizeof(Mat)), sizeof cog.m)) {
        g_xbowBarrelWhy.store("the crossbow's pose matrices did not read");
        return;
    }
    const xbowgrip::M inAttach = xbowgrip::Mul(xbowgrip::Mul(held.barrelInCog, cog), itemLocal);
    // [debug] xbow_barrel_line: does the game's own animation of the crossbow move its loaded bolt off it (a second bolt seen flying)?
    // The bolt bone against its bind place, in the cog's frame, as a part of the limbs' span; told when it leaves and when it is back.
    if (g_xbowBoltDiag.load(std::memory_order_relaxed) && held.bolt >= 0 && held.bolt < worldCount && held.span > 1e-4f) {
        xbowgrip::M boltNow, cogInverse;
        if (SafeCopy(boltNow.m, (const void*)(matrices + (uintptr_t)held.bolt * sizeof(Mat)), sizeof boltNow.m) && xbowgrip::Inverse(cog, &cogInverse)) {
            const xbowgrip::V d = xbowgrip::Sub(xbowgrip::Place(xbowgrip::Mul(boltNow, cogInverse)), held.boltBindInCog);
            const float off = std::sqrt(xbowgrip::Dot(d, d)) / held.span;
            const float size = std::sqrt(boltNow.m[0] * boltNow.m[0] + boltNow.m[1] * boltNow.m[1] + boltNow.m[2] * boltNow.m[2]);
            const bool away = off > 0.15f || size < 0.3f;
            if (away != held.boltAway && now - held.boltToldAt > 150) {
                held.boltAway = away;
                held.boltToldAt = now;
                char b[260];
                snprintf(b, sizeof b, "crossbow's own loaded bolt (%s, its \"bolt\" bone - the model's, not the missile): %s - %.2f of the limbs' span from its place, size %.2f",
                         held.model.c_str(), away ? "the game's animation takes it OFF the crossbow" : "back on the crossbow", off, size);
                XbowNote(b);
            }
        }
    }
    xbowgrip::Line check;
    if (!xbowgrip::FromBolt(inAttach, &check)) { g_xbowBarrelWhy.store("the crossbow's barrel did not come out as a line"); return; }
    AcquireSRWLockExclusive(&g_gunBoneLock);
    g_xbowBarrel = {inAttach, HeroGen(), now};
    g_xbowSource.self = self; g_xbowSource.cog = held.cog; g_xbowSource.count = held.count;
    g_xbowSource.barrelInCog = held.barrelInCog; g_xbowSource.gen = HeroGen(); g_xbowSource.at = now;
    ReleaseSRWLockExclusive(&g_gunBoneLock);
    g_xbowEntity.store(held.entity);
    g_xbowBarrelWhy.store(now - g_xbowRendered.at < 500 ? "read off the hero's crossbow, placed as the renderer draws it"
                                                         : "read off the hero's crossbow; the renderer's placing of it is not seen (yet) - the left hand stays on the line set by eye");
}

// The world-matrix function's result for an entity (potion_hide's hook on 0xE3A8C0 calls this after the original, renderer's thread): for
// the hero's crossbow, the relation between the item's world matrix as drawn and our attach bone in the world at the pose is learned, and
// the barrel put into the attach bone's frame through it (g_xbowRendered). The world translation is taken off both before the inverse
// (world coordinates run large). Once a hero model, it is compared in the log with the item's own transform (g_xbowBarrel).
void XbowRenderedIn(uint32_t entity, uintptr_t component, uintptr_t out) {
    (void)component;
    if (entity == 0xFFFFFFFFu || entity != g_xbowEntity.load(std::memory_order_relaxed)) return;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_gunBoneLock);
    const XbowSource src = g_xbowSource; const XbowAttach att = g_xbowAttach; const XbowBarrelOut approx = g_xbowBarrel;
    ReleaseSRWLockShared(&g_gunBoneLock);
    const uint32_t gen = HeroGen();
    if (!src.at || now - src.at > 500 || src.gen != gen || !att.at || now - att.at > 100 || att.gen != gen || src.cog < 0) return;
    uintptr_t world = 0, matrices = 0;
    int count = 0;
    xbowgrip::M cog, item;
    if (!Get(src.self + 0x20, &world) || !Get(world, &count) || count != src.count || !Get(world + 4, &matrices) || !matrices ||
        !SafeCopy(cog.m, (const void*)(matrices + (uintptr_t)src.cog * sizeof(Mat)), sizeof cog.m) || !SafeCopy(item.m, (const void*)out, sizeof item.m))
        return;
    xbowgrip::M relation;
    if (!xbowgrip::RenderedRelation(item, att.world, &relation)) return;
    const xbowgrip::M inAttach = xbowgrip::Mul(xbowgrip::Mul(src.barrelInCog, cog), relation);
    xbowgrip::Line line;
    if (!xbowgrip::FromBolt(inAttach, &line)) return;
    AcquireSRWLockExclusive(&g_gunBoneLock);
    g_xbowRendered = {inAttach, gen, now};
    ReleaseSRWLockExclusive(&g_gunBoneLock);
    static std::atomic<uint32_t> told{0xFFFFFFFFu};
    if (told.exchange(gen) != gen && att.upm > 1e-3f) {   // once a hero model: how far off the item's own transform was
        xbowgrip::Line own;
        char b[300];
        if (approx.at && approx.gen == gen && xbowgrip::FromBolt(approx.inAttach, &own)) {
            const xbowgrip::V d = xbowgrip::Sub(own.center, line.center);
            const float cm = std::sqrt(xbowgrip::Dot(d, d)) / att.upm * 100.0f;
            const float deg = std::acos(std::clamp(xbowgrip::Dot(own.dir, line.dir), -1.0f, 1.0f)) * 57.2957795f;
            snprintf(b, sizeof b, "crossbow barrel: the renderer's placing is seen; the item's own transform put the front of the stock %.1f cm and the barrel %.1f deg off it "
                     "(the hand uses the renderer's)", cm, deg);
        } else {
            snprintf(b, sizeof b, "crossbow barrel: the renderer's placing is seen (the hand uses it)");
        }
        XbowNote(b);
    }
}
std::string XbowNoteTakeIn() {
    std::string s;
    AcquireSRWLockExclusive(&g_xbowNoteLock); s.swap(g_xbowNote); ReleaseSRWLockExclusive(&g_xbowNoteLock);
    return s;
}
#endif

#if D2RVR_FIRST_PERSON
// ---- [multiplayer] pose_share_hero (vr/remote_pose.h): another player's rig posed from the pose that came ----------------------------------------------------
SRWLOCK g_remoteLock = SRWLOCK_INIT;
remotepose::Target g_remote[remotepose::kMaxHeroes];
std::atomic<unsigned> g_remoteWeapon{0};   // a weapon slot placed as the sender draws it (format 5)
std::atomic<unsigned> g_remoteHandsDrawn{0}, g_remoteReachMissMm{0};
struct RemoteHands { uint32_t unit = 0; ULONGLONG at = 0; bool ok[2] = {false, false}; float m[2][16] = {}; };   // [multiplayer] pose_share_held: RemoteHandWorld
SRWLOCK g_remoteHandsLock = SRWLOCK_INIT;
RemoteHands g_remoteHands[remotepose::kMaxHeroes];   // a hand placed as the sender draws it (format 6); the most it was out of the arm's reach (mm of his body)
std::atomic<unsigned> g_remoteLegs{0};    // [multiplayer] pose_share_legs: legs stepped by the gait library
std::atomic<unsigned> g_remoteYawFromWorld{0};   // poses turned by the world heading through the rig's matrix (else the sender's yaw in his model)

// [multiplayer] pose_share_legs: each other hero's legs, his rig by its SkeletonInstance - the gait's own memory (the library keeps none) and his travel,
// from his rig's place frame to frame (ecs::BodyPlace, the look-at's units), smoothed.
std::atomic<float> g_gaitCone{45.0f};   // [body] gait_cone_deg: the half-angle of the cone the game's own walk keeps (SetGaitCone)
struct LegsMem { D2rfState st{}; float x = 0.0f, z = 0.0f, vx = 0.0f, vz = 0.0f; LARGE_INTEGER at{}; bool have = false; ULONGLONG seen = 0;
                 remotepose::ConeBlend cone;   // our gait's share against the game's walk, by the travel's angle from the facing
                 float faceX = 0.0f, faceZ = 0.0f; ULONGLONG faceAt = 0;   // (a hero with no pose: his facing followed, the world's ground direction)
                 float dirWX = 0.0f, dirWZ = 0.0f; bool dirWorld = false; };   // the library's way of the steps, in the world (remotepose::DirModelToWorld)
// What the legs did since the last LegsNote (under g_legsLock): the rigs stepped, how far each foot was put from where the game had it, the swing's lift,
// the walk's share and speed, the hips' drop; and whether our write was still in the rig's bones when the game posed it next (kept) or something had
// written over it (lost) - the test of whether what we write is what is drawn.
struct LegsDiag {
    uintptr_t rigs[8] = {};
    int nRigs = 0;
    unsigned steps = 0, kneeFail = 0, kept = 0, lost = 0, gameWalk = 0;   // gameWalk: poses left wholly to the game's walk (in the cone, or standing)
    unsigned reversals = 0, reverseRefused = 0;   // the steps' way turned round at once on a reversal (remotepose::ReverseSteps); not, the library's feet moved by it
    float shareSum = 0.0f;
    float footCmMax = 0.0f, footCmSum = 0.0f, liftCmMax = 0.0f, dropCmMax = 0.0f, moveSum = 0.0f, speedSum = 0.0f, speedMax = 0.0f;
    unsigned feet = 0;
};
LegsDiag g_legsDiag;
// the feet as we last wrote them, per rig: checked against the rig's bones at its next pose, before the game makes it again
struct LegWrite { int foot[2] = {-1, -1}; float pos[2][3] = {}; ULONGLONG at = 0; };
std::map<uintptr_t, LegWrite> g_legWrites;   // under g_legsLock
SRWLOCK g_legsLock = SRWLOCK_INIT;
std::map<uintptr_t, LegsMem> g_legsMem;

// His legs on the gait library's feet: the whole body already turned to his look (the pelvis faces where he faces, never his path), the travel turned
// into his model (remotepose::TravelInModel), the steps along it - forward, backward, sideways - the hips down as far as the legs need, each knee by our
// two-bone leg (legik::Solve, the knee ahead; the thigh and the shin turned from their rest, legik::Turn), each foot flat and facing the body (its bind
// turn turned with the body), its toes with it.
// r: the received pose (nullptr: none - a player who sends no pose: the game's facing, an eye height of 1.7 m); M: the rig's model -> world (nullptr: not read).
void RemoteLegs(uintptr_t self, const Hero& h, const remotepose::Target* r, float x, float z, float c, float s, const Mat& T, const Mat* M, std::vector<Mat>& newW,
                const std::vector<Mat>& gameW) {
    const int n = h.count;
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    D2rfLegsIn in{};
    in.size = sizeof in;
    D2rfState st{};
    float vx = 0.0f, vz = 0.0f;
    {
        AcquireSRWLockExclusive(&g_legsLock);
        const ULONGLONG tick = GetTickCount64();
        if (g_legsMem.size() > 32) std::erase_if(g_legsMem, [&](const auto& e) { return tick - e.second.seen > 5000; });
        LegsMem& m = g_legsMem[self];
        const double dt = m.have && f.QuadPart ? (double)(now.QuadPart - m.at.QuadPart) / (double)f.QuadPart : 0.0;
        if (!m.have || dt > 0.3) { m.vx = m.vz = 0.0f; }
        else if (dt > 1e-4) {
            const float k = 1.0f - (float)std::exp(-dt / 0.08);
            m.vx += ((x - m.x) / (float)dt - m.vx) * k;
            m.vz += ((z - m.z) / (float)dt - m.vz) * k;
        }
        m.x = x; m.z = z; m.at = now; m.have = true; m.seen = tick;
        in.dt = dt > 0.3 ? 0.0f : (float)dt;
        st = m.st;
        // the steps' way into the model as the game has turned it now: walking back the game turns the model round to the path at once, and a way kept in the old
        // model swept through sideways for half a second while the library turned it back - a foot stepped across the other's (the 2026-10-27 crossed legs)
        if (M && m.dirWorld && (st.dirX != 0.0f || st.dirZ != 0.0f)) remotepose::DirWorldToModel(M->m, m.dirWX, m.dirWZ, &st.dirX, &st.dirZ);
        vx = m.vx; vz = m.vz;
        ReleaseSRWLockExclusive(&g_legsLock);
    }
    const float upm = remotepose::ModelUpm(Pos(newW[h.head]).y, r ? r->eyeM : 1.7f);
    float vel[3] = {0.0f, 0.0f, 0.0f};
    if (M) {   // the world's travel back through the rig's own matrix (its turn and size): exact, whatever the pose says
        const Mat inv = InvAffine(*M);
        const V3 v = RotOnly({vx, 0.0f, vz}, inv);
        vel[0] = v.x; vel[2] = v.z;
    } else if (r) {
        remotepose::TravelInModel(vx, vz, r->heading, c, s, upm, r->worldUpm, vel);
    }
    in.unitsPerM = upm;
    in.forward = {s, 0.0f, c};
    in.velocity = {vel[0], vel[1], vel[2]};
    for (int side = 0; side < 2; ++side) {
        const int hip = h.leg[side][0], knee = h.leg[side][1], foot = h.leg[side][2];
        const V3 hp = Pos(newW[hip]), rest = Through(T, Pos(h.bind[foot]));
        in.hip[side] = {hp.x, hp.y, hp.z};
        in.rest[side] = {rest.x, rest.y, rest.z};
        in.legLength[side] = Len(Sub(Pos(h.bind[knee]), Pos(h.bind[hip]))) + Len(Sub(Pos(h.bind[foot]), Pos(h.bind[knee])));
    }
    D2rfLegsOut out{};
    out.size = sizeof out;
    {   // a reversal (forward, now back): the steps' way turned round at once and the stride mirrored, so the feet stay where they stand - kept only when the library
        // puts both feet in the same place for it (both states at dt 0); else as before (its own turn, through sideways: the 2026-10-27 crossed legs)
        D2rfState flip = st;
        const float speedNow = std::sqrt(vel[0] * vel[0] + vel[2] * vel[2]) / std::max(upm, 1e-3f);
        if (speedNow > 0.1f && remotepose::ReverseSteps(&flip.dirX, &flip.dirZ, &flip.phase, vel[0], vel[2])) {
            D2rfLegsIn still = in;
            still.dt = 0.0f;
            D2rfState a = st, b = flip;
            D2rfLegsOut oa{}, ob{};
            oa.size = ob.size = sizeof oa;
            float worst = 1e30f;
            if (gaitlib::Step(still, &a, &oa) && gaitlib::Step(still, &b, &ob)) {
                worst = 0.0f;
                for (int side = 0; side < 2; ++side)
                    worst = std::max(worst, Len(Sub(V3{oa.foot[side].x, oa.foot[side].y, oa.foot[side].z}, V3{ob.foot[side].x, ob.foot[side].y, ob.foot[side].z})));
            }
            const bool same = worst < 0.01f * upm;   // (a centimetre)
            if (same) st = flip;
            AcquireSRWLockExclusive(&g_legsLock);
            ++(same ? g_legsDiag.reversals : g_legsDiag.reverseRefused);
            ReleaseSRWLockExclusive(&g_legsLock);
        }
    }
    if (!gaitlib::Step(in, &st, &out)) return;   // (stepped always: its phase goes on while the game's walk shows)
    // [body] gait_cone_deg: the game's own walk within the cone about the facing, ours outside it, crossfaded
    const float speedM = std::sqrt(vel[0] * vel[0] + vel[2] * vel[2]) / std::max(upm, 1e-3f);
    const float angle = remotepose::TravelAngleDeg(vel[0], vel[2], s, c);
    float w = 0.0f;
    {
        AcquireSRWLockExclusive(&g_legsLock);
        LegsMem& m = g_legsMem[self];
        m.st = st;
        m.dirWorld = M && (st.dirX != 0.0f || st.dirZ != 0.0f) && remotepose::DirModelToWorld(M->m, st.dirX, st.dirZ, &m.dirWX, &m.dirWZ);
        w = m.cone.Step(angle, speedM > 0.15f && angle >= 0.0f, g_gaitCone.load(std::memory_order_relaxed), in.dt);
        g_legsDiag.shareSum += w;
        if (w <= 0.001f) { ++g_legsDiag.steps; ++g_legsDiag.gameWalk; g_legWrites.erase(self); }   // (nothing of ours to check at the next pose)
        ReleaseSRWLockExclusive(&g_legsLock);
    }
    if (w <= 0.001f) return;   // the game's own legs, untouched
    V3 gameFoot[2];   // the game's own feet (the body turned, before the hips drop): crossfaded from, w of the way to the gait's
    for (int side = 0; side < 2; ++side) gameFoot[side] = Pos(newW[h.leg[side][2]]);
    if (out.drop > 0.0f && std::isfinite(out.drop))
        for (int i = 0; i < n; ++i) newW[i].m[13] -= out.drop * w;
    unsigned kneeFail = 0;
    for (int side = 0; side < 2; ++side) {
        const int hip = h.leg[side][0], knee = h.leg[side][1], foot = h.leg[side][2];
        const std::vector<Mat> base = newW;
        const V3 H = Pos(newW[hip]);
        const float thigh = Len(Sub(Pos(newW[knee]), H)), shin = Len(Sub(Pos(newW[foot]), Pos(newW[knee])));
        const V3 g = gameFoot[side];
        const legik::V F{g.x + (out.foot[side].x - g.x) * w, g.y + (out.foot[side].y - g.y) * w, g.z + (out.foot[side].z - g.z) * w};
        // the knee and both bones' turns from the rig's rest (turned with the body) and the leg's bend plane (vr/leg_ik.h) - never the shortest swing from
        // the game's walk pose, which turned the bone round its length when the game's leg was ahead and ours behind (the kneecap to the back)
        auto L = [](V3 v) { return legik::V{v.x, v.y, v.z}; };
        const V3 hip0 = Through(T, Pos(h.bind[hip])), knee0 = Through(T, Pos(h.bind[knee])), foot0 = Through(T, Pos(h.bind[foot]));
        const legik::V ahead = legik::KneeAhead(L(hip0), L(knee0), L(foot0), {s, 0.0f, c});
        legik::Bend b;
        float Rt[9], Rs[9];
        if (!legik::Solve(L(H), thigh, shin, F, ahead, legik::kMaxFlexDeg, b)) { ++kneeFail; continue; }
        const legik::V h0 = legik::Hinge(L(hip0), L(foot0), ahead);
        if (!legik::Turn(L(Sub(knee0, hip0)), h0, b.knee - L(H), b.hinge, Rt) || !legik::Turn(L(Sub(foot0, knee0)), h0, b.foot - b.knee, b.hinge, Rs)) {
            ++kneeFail;
            continue;
        }
        auto turned = [&](int bone, const float R[9], V3 at) {   // the bone at rest (turned with the body), turned by R, at `at`
            Mat r{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = R[i * 3 + j];
            r.m[15] = 1.0f;
            Mat m = Mul(Mul(h.bind[bone], T), r);
            m.m[12] = at.x; m.m[13] = at.y; m.m[14] = at.z;
            return m;
        };
        newW[hip] = turned(hip, Rt, H);
        {
            std::vector<char> moved(n, 0);
            moved[hip] = 1;
            Propagate(h, base, newW, moved, hip);
        }
        newW[knee] = turned(knee, Rs, Pos(newW[knee]));
        {
            std::vector<char> moved(n, 0);
            moved[knee] = 1;
            Propagate(h, base, newW, moved, knee);
        }
        if (w >= 0.5f) {   // the gait's foot: flat, facing the body (under half way, the game's foot keeps its own turn)
            const V3 at = Pos(newW[foot]);
            newW[foot] = Mul(h.bind[foot], T);
            newW[foot].m[12] = at.x; newW[foot].m[13] = at.y; newW[foot].m[14] = at.z;
        }
        std::vector<char> moved(n, 0);
        moved[foot] = 1;
        Propagate(h, base, newW, moved, foot);
    }
    {   // what this step changed against the game's own legs, and the feet as written (checked at the rig's next pose)
        AcquireSRWLockExclusive(&g_legsLock);
        LegsDiag& d = g_legsDiag;
        bool known = false;
        for (int i = 0; i < d.nRigs; ++i) known = known || d.rigs[i] == self;
        if (!known && d.nRigs < 8) d.rigs[d.nRigs++] = self;
        ++d.steps;
        d.kneeFail += kneeFail;
        const float cm = 100.0f / std::max(upm, 1e-3f);
        for (int side = 0; side < 2; ++side) {
            const int foot = h.leg[side][2];
            const float off = Len(Sub(Pos(newW[foot]), Pos(gameW[foot]))) * cm;
            if (std::isfinite(off)) { d.footCmMax = std::max(d.footCmMax, off); d.footCmSum += off; ++d.feet; }
            d.liftCmMax = std::max(d.liftCmMax, out.lift[side] * 0.10f * 100.0f);   // (the library's lift is 0..1 of its 10 cm clearance for a man's leg)
        }
        d.dropCmMax = std::max(d.dropCmMax, out.drop * cm);
        d.moveSum += out.move;
        d.speedSum += out.speedM;
        d.speedMax = std::max(d.speedMax, out.speedM);
        LegWrite& w = g_legWrites[self];
        for (int side = 0; side < 2; ++side) {
            const int foot = h.leg[side][2];
            w.foot[side] = foot;
            w.pos[side][0] = newW[foot].m[12]; w.pos[side][1] = newW[foot].m[13]; w.pos[side][2] = newW[foot].m[14];
        }
        w.at = GetTickCount64();
        if (g_legWrites.size() > 32) std::erase_if(g_legWrites, [&](const auto& e) { return w.at - e.second.at > 2000; });
        ReleaseSRWLockExclusive(&g_legsLock);
    }
    g_remoteLegs.fetch_add(1, std::memory_order_relaxed);
}

// [body] walk_back: our own hero turned whole to the look (PoseBody's bodyTurn 2) while the game walks him the other way. The game faces him down his path and
// plays its forward walk, turned half a turn with him: his legs walked forward while he went back - the planted foot slid along at twice his speed, the swinging
// leg passed it the wrong way ("the legs cross over each other when I move backwards", 2026-10-27). His travel (his rig's place frame to frame, through its
// matrix into his model) more than kWalkBackFromDeg from where he faces - crossfaded by remotepose::ConeBlend, with its hysteresis and hold - each foot's place
// along his facing is mirrored about his hips (legik::MirrorAlong): the planted foot stands, the swing goes back; the knee solved again ahead of the hip-ankle
// line (legik::Solve / Turn, as the gait's), the foot keeping the game's own turn. Across and up nothing changes: the left foot stays left. Drawn bones only.
// [body] walk_back_reverse (on): Serg's 2026-10-28 test - "when the hero walks backwards his legs still cross". The mirror above is the walk of a hero facing
// the other way with his legs on the wrong sides: the foot lifted high in front of him, set down behind him toes up. Instead the game's own walk is played
// BACKWARDS (vr/walk_back.h): his lower body - the pelvis and everything under it but the trunk - is kept from every pose of his walking (any way: the game
// always plays its forward walk), the body's turn taken out, and while he walks back each pose is drawn from a cursor going back through that store: the
// planted foot stands, the heel lifts behind him, the toes reach back to land - every joint the game's own. The trunk rides on that pelvis (as the game hangs
// it) unless [body] lock holds it in its bind pose. Nothing kept yet (his very first steps): the mirror for those frames.
// Still crossed in his 2026-10-28 17:03 test; drawn offline (tools/walk_legs: the game's 1hs_walk, and this Reverser fed with it) both the walk played
// backwards and the Reverser make an honest backward step. What the offline pictures lacked and his log shows: the game turns the hero at every change
// of way on the skeleton's anim bone (up to 128 deg seen, for ~0.1-0.2 s) while his matrix has turned already - the drawn legs faced the way he went
// under the locked trunk, and the store began each walk back with those poses. That turn is taken out first (walk_back.h TurnAbout). And an area change
// moved his place 11511 units in a frame: the smoothed travel "walked him back" ~1.5 s while he stood - a jump of the place reads no travel (kJumpMps).
constexpr float kWalkBackFromDeg = 100.0f;
static_assert(sizeof(Mat) == sizeof(walkback::M4), "walk_back.h's M4 is skeletons.cpp's Mat");
struct WalkBackMem {
    float x = 0.0f, z = 0.0f, vx = 0.0f, vz = 0.0f; LARGE_INTEGER at{}; bool have = false; uintptr_t self = 0; remotepose::ConeBlend cone;
    walkback::Reverser rev;                                // [body] walk_back_reverse: the game's walk kept, and played back
    std::vector<int> lower;                                // ... the bones kept: the pelvis and every bone under it but the trunk
    std::vector<int> trunk;                                // ... then the trunk (every other bone under the pelvis), played with its pelvis
    int gait = 0;                                          // ... the game's walk he is in (its mode: 2 walk, 3 run, 6 town walk; 0 not known yet)
    std::vector<walkback::M4> pose;                        // ... this pose's, the body's turn taken out
    walkback::ModeGate gate;                               // the game's standing mode, once it has held (not a frame's dip)
    float modelYaw = 0.0f; bool haveModelYaw = false;      // his matrix's facing at the last pose (a jump of it: the game turned him)
    double turnUntil = -1.0;                               // ... the game's turn in the skeleton taken out until then
};
WalkBackMem g_walkBack;   // (the skeleton hook's thread: our own hero only)
std::atomic<unsigned> g_walkBackPoses{0}, g_walkBackFull{0}, g_walkBackKneeFail{0}, g_walkBackPlayed{0}, g_walkBackJumps{0};
std::atomic<float> g_walkBackAngle{0.0f}, g_walkBackKeptS{0.0f};
std::atomic<unsigned> g_walkBackSeen{0}, g_walkBackSlow{0}, g_walkBackOtherMode{0};   // poses seen while he moved or walked back; too slow; not the game's walk mode
std::atomic<int> g_walkBackMode{-1};   // the last mode that was not a walk
std::atomic<unsigned> g_walkBackAhead{0}, g_walkBackPlaceJumps{0}, g_walkBackTurnOut{0};   // on while he went ahead (< 90 deg); his place jumped; the game's turn taken out
std::atomic<unsigned> g_walkBackWhole{0}, g_walkBackModeDips{0};   // came on wholly at once (on the game's own pose); a standing mode let pass (not held kStandS)
std::atomic<float> g_walkBackTurnMaxDeg{0.0f};   // ... the most of it
// [debug] walk_back_dump: what one call saw, for the dump (WalkBackLegs below)
struct WalkBackRow { double t = 0.0; float dt = 0.0f, speedM = 0.0f, angle = -1.0f, w = 0.0f, turnDeg = 0.0f, vx = 0.0f, vz = 0.0f, sx = 0.0f, sz = 0.0f, upm = 0.0f; };
WalkBackRow g_wbRow;   // (the skeleton hook's thread)
void WalkBackLegsIn(uintptr_t self, const Hero& h, const Input& in, float c, float s, std::vector<Mat>& newW) {
    const float userHeadM = in.userHeadM;
    const int n = h.count;
    float x = 0.0f, z = 0.0f;
    Mat M{};
    if (!ecs::BodyPlace(self, &x, &z, nullptr, 500, M.m)) return;
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    WalkBackMem& m = g_walkBack;
    if (m.self != self) m = WalkBackMem{};
    m.self = self;
    const double dt = m.have && f.QuadPart ? (double)(now.QuadPart - m.at.QuadPart) / (double)f.QuadPart : 0.0;
    const float upm = std::clamp((h.head >= 0 ? Pos(newW[h.head]).y : h.eyeY) / std::max(0.8f, userHeadM), 2.0f, 8.0f);   // model units a metre
    const V3 stepIn = m.have ? RotOnly({x - m.x, 0.0f, z - m.z}, InvAffine(M)) : V3{0.0f, 0.0f, 0.0f};   // (into his model)
    g_wbRow.sx = stepIn.x; g_wbRow.sz = stepIn.z; g_wbRow.upm = upm; g_wbRow.dt = (float)dt;
    if (m.have && dt > 1e-4 && dt <= 0.3 && Len(stepIn) / upm / (float)dt > walkback::kJumpMps) {
        m.vx = m.vz = 0.0f;   // his place jumped (an area change, a waypoint): no travel from it (walk_back.h kJumpMps)
        m.cone = remotepose::ConeBlend{};
        g_walkBackPlaceJumps.fetch_add(1, std::memory_order_relaxed);
    } else if (!m.have || dt > 0.3) { m.vx = m.vz = 0.0f; }
    else if (dt > 1e-4) {   // the travel in the world (the look-at's units a second), smoothed as the other heroes' (0.08 s)
        const float k = 1.0f - (float)std::exp(-dt / 0.08);
        m.vx += ((x - m.x) / (float)dt - m.vx) * k;
        m.vz += ((z - m.z) / (float)dt - m.vz) * k;
    }
    m.x = x; m.z = z; m.at = now; m.have = true;
    const V3 v = RotOnly({m.vx, 0.0f, m.vz}, InvAffine(M));   // into his model (its turn and size: exact, as the gait's)
    const float speedM = std::sqrt(v.x * v.x + v.z * v.z) / upm;
    const float angle = remotepose::TravelAngleDeg(v.x, v.z, s, c);
    // he goes only while the game plays its walk or run (walkback::GameWalkMode: standing, the rig's place still read a speed now and then, any way); the game
    // says he stands: no hold, the walk back fades out at once - once it has said so for walkback::ModeGate::kStandS (a frame's mode 1 while he walks
    // dropped the cone and dithered the share: the scissor of the 2026-10-29 test)
    const bool stands = m.gate.Stands(in.heroAnimMode, dt > 0.3 ? 0.0f : (float)dt);
    const bool gameWalk = !stands;
    if (!stands && walkback::GameStands(in.heroAnimMode) && speedM > 0.15f) g_walkBackModeDips.fetch_add(1, std::memory_order_relaxed);
    if (stands) m.cone.still = remotepose::ConeBlend::kHoldStandS + 1.0f;
    const float wasW = m.cone.share;
    float w = m.cone.Step(angle, speedM > 0.15f && angle >= 0.0f && gameWalk, kWalkBackFromDeg, dt > 0.3 ? 0.0f : (float)dt);
    g_walkBackAngle.store(angle);
    // his matrix turned at once (the game's change of way): its turn in the skeleton is taken out for a while (below)
    const double nowS = f.QuadPart ? (double)now.QuadPart / (double)f.QuadPart : 0.0;
    const float modelYaw = std::atan2(M.m[8], M.m[10]);
    if (m.haveModelYaw && std::fabs(walkback::WrapAngle(modelYaw - m.modelYaw)) > walkback::kFlipDeg * 0.0174532925f) m.turnUntil = nowS + walkback::kTurnS;
    m.modelYaw = modelYaw;
    m.haveModelYaw = true;
    g_wbRow.speedM = speedM; g_wbRow.angle = angle; g_wbRow.w = w; g_wbRow.vx = v.x; g_wbRow.vz = v.z;
    g_wbRow.t = f.QuadPart ? (double)now.QuadPart / (double)f.QuadPart : 0.0;
    if (w > 0.001f && angle >= 0.0f && angle < 90.0f && speedM > 0.15f) g_walkBackAhead.fetch_add(1, std::memory_order_relaxed);   // (the log: on while he went ahead)
    Mat T{}; T.m[0] = c; T.m[2] = -s; T.m[5] = 1; T.m[8] = s; T.m[10] = c; T.m[15] = 1;   // (the turn PoseBody gave the body: the legs' rest turned with it)
    // The game's own turn, played on the skeleton's anim bone (the pelvis's parent) when he changes his way (walk_back.h TurnAbout): taken out of all that
    // hangs on it - the trunk too, unless [body] lock holds it in its bind pose already - so the body faces the look, and the walk kept below is the walk
    // alone. Only in the game's walk and standing modes (a skill that spins him - its own turn of the anim bone - keeps it), and only within kTurnS of his
    // matrix turning at once: otherwise the anim bone's yaw is his stance (the paladin's stood a few degrees off its bind pose: 852 poses "taken out" in
    // the 2026-10-29 log while he only stood). The game's walk has the anim bone at 0 (the "facing:" lines: anim 0.0), so the turn is its yaw as it is.
    const int animBone = h.pelvis >= 0 ? h.parent[h.pelvis] : -1;
    const int mode = in.heroAnimMode;
    if (animBone >= 0 && animBone < n && nowS < m.turnUntil && (mode < 0 || mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 6)) {
        walkback::M4 a{};
        memcpy(a.m, newW[animBone].m, sizeof a.m);
        const float turnIn = walkback::WrapAngle(walkback::YawOf(a) - std::atan2(s, c));
        if (std::fabs(turnIn) > 0.035f) {   // (2 deg; the anim bone carries no sway of the walk's - only the turn)
            const float px = newW[animBone].m[12], pz = newW[animBone].m[14];
            std::vector<char> under(n, 0);
            under[animBone] = 1;
            for (int i = 0; i < n; ++i) {
                const int p = h.parent[i];
                if (p >= 0 && p < i && under[p]) under[i] = 1;
                if (!under[i] || (in.lockUpper && h.upper[i])) continue;
                walkback::M4 b{};
                memcpy(b.m, newW[i].m, sizeof b.m);
                walkback::TurnAbout(b, -turnIn, px, pz);
                memcpy(newW[i].m, b.m, sizeof b.m);
            }
            g_walkBackTurnOut.fetch_add(1, std::memory_order_relaxed);
            const float deg = std::fabs(turnIn) * 57.2957795f;
            g_wbRow.turnDeg = turnIn * 57.2957795f;
            if (deg > g_walkBackTurnMaxDeg.load()) g_walkBackTurnMaxDeg.store(deg);
        }
    }
    if (in.walkBackReverse && h.pelvis >= 0) {
        // the game's own lower body of this pose, kept while he walks (whichever way), the body's turn taken out
        if (!m.lower.empty() && (m.lower.front() != h.pelvis || m.lower.back() >= n || (!m.trunk.empty() && m.trunk.back() >= n))) {   // (another rig here)
            m.lower.clear();
            m.trunk.clear();
            m.rev.Reset();
        }
        if (m.lower.empty()) {
            std::vector<char> low(n, 0), under(n, 0);
            for (int i = 0; i < n; ++i) {
                const int p = h.parent[i];
                under[i] = i == h.pelvis || (p >= 0 && p < i && under[p]);
                low[i] = i == h.pelvis || (!h.upper[i] && p >= 0 && p < i && low[p]);
                if (low[i]) m.lower.push_back(i);
                else if (under[i]) m.trunk.push_back(i);
            }
        }
        // the game's gait now (walk, run, town walk); a frame's other mode (ModeGate let it pass) keeps the last
        if (walkback::GameWalkMode(in.heroAnimMode) && in.heroAnimMode >= 0) m.gait = in.heroAnimMode;
        int footAt[2] = {-1, -1};
        for (int k = 0; k < (int)m.lower.size(); ++k)
            for (int side = 0; side < 2; ++side)
                if (m.lower[k] == h.leg[side][2]) footAt[side] = k;
        if (footAt[0] >= 0 && footAt[1] >= 0) {
            Mat Ti{}; Ti.m[0] = c; Ti.m[2] = s; Ti.m[5] = 1; Ti.m[8] = -s; Ti.m[10] = c; Ti.m[15] = 1;   // (T's inverse: the body's turn out)
            // the trunk of the same pose kept after the lower body: played back with its pelvis (F2: the game hangs the trunk on the pelvis with a
            // counter-sway of the SAME moment of the stride - hung on a pelvis of another moment, the two sways added: the body rocked from side to
            // side running back, Serg's 2026-10-29 "it began to sway")
            const size_t nl = m.lower.size(), kept = nl + m.trunk.size();
            m.pose.resize(kept);
            for (size_t k = 0; k < kept; ++k) {
                const Mat b = Mul(newW[k < nl ? m.lower[k] : m.trunk[k - nl]], Ti);
                memcpy(m.pose[k].m, b.m, sizeof b.m);
            }
            float feet[6];
            for (int side = 0; side < 2; ++side)
                for (int j = 0; j < 3; ++j) feet[side * 3 + j] = m.pose[footAt[side]].m[12 + j];
            const double tS = f.QuadPart ? (double)now.QuadPart / (double)f.QuadPart : 0.0;
            if (speedM > 0.15f || w > 0.001f) {   // (for the log: why a pose while he goes was not taken as his walk)
                g_walkBackSeen.fetch_add(1, std::memory_order_relaxed);
                if (!(speedM > 0.15f)) g_walkBackSlow.fetch_add(1, std::memory_order_relaxed);
                if (!gameWalk) { g_walkBackOtherMode.fetch_add(1, std::memory_order_relaxed); g_walkBackMode.store(in.heroAnimMode); }
            }
            m.rev.Record(tS, speedM > 0.15f && gameWalk, m.pose.data(), (int)m.pose.size(), feet, m.gait);
            g_walkBackKeptS.store((float)m.rev.KeptS());
            // coming on: the play-back starts on the newest pose kept - when that is this very pose (the game's of this frame), it IS what the game draws, so
            // it is drawn wholly at once; crossfaded in, the game's walk forward and the walk backwards (a leg ahead in one, behind in the other) passed the
            // feet through each other for the fade (the 2026-10-29 scissor)
            if (wasW <= 0.001f && w > 0.001f && m.rev.Kept() > 0 && m.rev.At(m.rev.Kept() - 1).t == tS) {
                m.cone.share = 1.0f;
                w = 1.0f;
                g_wbRow.w = w;
                g_walkBackWhole.fetch_add(1, std::memory_order_relaxed);
            }
            const walkback::Sample* back = m.rev.Step(dt > 0.3 ? 0.0 : dt, w > 0.001f, m.gait);
            g_walkBackJumps.fetch_add((unsigned)m.rev.TakeJumps(), std::memory_order_relaxed);
            if (w <= 0.001f) return;
            if (back && back->pose.size() == m.lower.size() + m.trunk.size()) {   // played back: the kept pose, turned with the body again, w of the way from the game's
                // the lower body - and the trunk of the same kept pose, unless [body] lock holds the trunk in its bind pose (F4)
                const size_t nl = m.lower.size(), upto = in.lockUpper ? nl : nl + m.trunk.size();
                for (size_t k = 0; k < upto; ++k) {
                    const int bone = k < nl ? m.lower[k] : m.trunk[k - nl];
                    Mat b{};
                    memcpy(b.m, back->pose[k].m, sizeof b.m);
                    b = Mul(b, T);
                    walkback::M4 a{}, to{};
                    memcpy(a.m, newW[bone].m, sizeof a.m);
                    memcpy(to.m, b.m, sizeof to.m);
                    const walkback::M4 r = walkback::Blend(a, to, w);
                    memcpy(newW[bone].m, r.m, sizeof r.m);
                }
                g_walkBackPoses.fetch_add(1, std::memory_order_relaxed);
                g_walkBackPlayed.fetch_add(1, std::memory_order_relaxed);
                if (w >= 0.999f) g_walkBackFull.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }
    if (w <= 0.001f) return;
    const V3 hipL = Pos(newW[h.leg[1][0]]), hipR = Pos(newW[h.leg[0][0]]);
    const legik::V centre{(hipL.x + hipR.x) * 0.5f, 0.0f, (hipL.z + hipR.z) * 0.5f}, forward{s, 0.0f, c};
    auto L = [](V3 q) { return legik::V{q.x, q.y, q.z}; };
    bool fail = false;
    for (int side = 0; side < 2; ++side) {
        const int hip = h.leg[side][0], knee = h.leg[side][1], foot = h.leg[side][2];
        const std::vector<Mat> base = newW;
        const V3 H = Pos(newW[hip]), game = Pos(newW[foot]);
        const Mat footTurn = newW[foot];   // the game's own turn of the foot (turned with the body): kept
        const float thigh = Len(Sub(Pos(newW[knee]), H)), shin = Len(Sub(game, Pos(newW[knee])));
        const legik::V F = legik::MirrorAlong(L(game), centre, forward, w);
        const V3 hip0 = Through(T, Pos(h.bind[hip])), knee0 = Through(T, Pos(h.bind[knee])), foot0 = Through(T, Pos(h.bind[foot]));
        const legik::V ahead = legik::KneeAhead(L(hip0), L(knee0), L(foot0), forward);
        legik::Bend b;
        float Rt[9], Rs[9];
        const legik::V h0 = legik::Hinge(L(hip0), L(foot0), ahead);
        if (!legik::Solve(L(H), thigh, shin, F, ahead, legik::kMaxFlexDeg, b) || !legik::Turn(L(Sub(knee0, hip0)), h0, b.knee - L(H), b.hinge, Rt) ||
            !legik::Turn(L(Sub(foot0, knee0)), h0, b.foot - b.knee, b.hinge, Rs)) {
            fail = true;
            continue;
        }
        auto turned = [&](int bone, const float R[9], V3 at) {   // the bone at rest (turned with the body), turned by R, at `at`
            Mat r{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = R[i * 3 + j];
            r.m[15] = 1.0f;
            Mat mm = Mul(Mul(h.bind[bone], T), r);
            mm.m[12] = at.x; mm.m[13] = at.y; mm.m[14] = at.z;
            return mm;
        };
        newW[hip] = turned(hip, Rt, H);
        { std::vector<char> moved(n, 0); moved[hip] = 1; Propagate(h, base, newW, moved, hip); }
        newW[knee] = turned(knee, Rs, Pos(newW[knee]));
        { std::vector<char> moved(n, 0); moved[knee] = 1; Propagate(h, base, newW, moved, knee); }
        const V3 at = Pos(newW[foot]);
        newW[foot] = footTurn;
        newW[foot].m[12] = at.x; newW[foot].m[13] = at.y; newW[foot].m[14] = at.z;
        std::vector<char> moved(n, 0);
        moved[foot] = 1;
        Propagate(h, base, newW, moved, foot);
    }
    g_walkBackPoses.fetch_add(1, std::memory_order_relaxed);
    if (w >= 0.999f) g_walkBackFull.fetch_add(1, std::memory_order_relaxed);
    if (fail) g_walkBackKneeFail.fetch_add(1, std::memory_order_relaxed);
}
// [debug] walk_back_dump: the last kWalkBackDumpS of his moving, pose by pose, handed to the log tick once a walk back has ended (TakeWalkBackDump) - for
// tools/walk_legs/walk_legs.py --dump, which draws the game's legs and the drawn ones as the offline pictures. Only kept while it is on; view only.
constexpr double kWalkBackDumpS = 6.0;
constexpr int kWalkBackDumpsAtMost = 30;   // (a session's file stays under ~10 MB)
std::deque<std::pair<double, std::string>> g_wbLines;   // (the skeleton hook's thread)
double g_wbOffAt = -1.0;                                  // the walk back's share went to 0 here (-1: on, or none in the lines)
bool g_wbHadOn = false;
double g_wbTurnSince = -1.0, g_wbTurnDumpAt = -100.0;   // standing turned round since; the last such dump
int g_wbDumps = 0;
SRWLOCK g_wbLock = SRWLOCK_INIT;
std::string g_wbReady;   // (under g_wbLock)
void WalkBackJoints(const Hero& h, const std::vector<Mat>& W, std::string& out) {   // pelvis, spine, head, then per side hip, knee, foot and the foot's z row
    char b[64];
    auto put = [&](float x, float y, float z) { snprintf(b, sizeof b, " %.3f %.3f %.3f", x, y, z); out += b; };
    auto at = [&](int i) { if (i >= 0 && i < (int)W.size()) put(W[i].m[12], W[i].m[13], W[i].m[14]); else put(0.0f, 0.0f, 0.0f); };
    at(h.pelvis); at(h.spine); at(h.head);
    for (int side = 0; side < 2; ++side) {
        for (int k = 0; k < 3; ++k) at(h.leg[side][k]);
        const int f = h.leg[side][2];
        if (f >= 0 && f < (int)W.size()) put(W[f].m[8], W[f].m[9], W[f].m[10]); else put(0.0f, 0.0f, 0.0f);
    }
}
void WalkBackLegs(uintptr_t self, const Hero& h, const Input& in, float c, float s, std::vector<Mat>& newW) {
    if (!g_walkBackDumpOn.load(std::memory_order_relaxed)) { WalkBackLegsIn(self, h, in, c, s, newW); return; }
    g_wbRow = WalkBackRow{};
    std::string line;
    WalkBackJoints(h, newW, line);   // the game's, turned with the body
    WalkBackLegsIn(self, h, in, c, s, newW);
    const WalkBackRow& r = g_wbRow;
    char b[320];
    snprintf(b, sizeof b, "%.4f %.4f %d %.3f %.1f %.3f %.1f %.2f %.3f %.3f %.4f %.4f %.3f |", r.t, r.dt, in.heroAnimMode, r.speedM, r.angle, r.w, r.turnDeg,
             std::atan2(s, c) * 57.2957795f, r.vx, r.vz, r.sx, r.sz, r.upm);
    line = b + line + " |";
    WalkBackJoints(h, newW, line);   // as drawn
    if (r.t <= 0.0) return;
    // standing with the body turned far from the game's facing (the camera turned round him: Serg's 2026-10-29 "the feet stay where they were") is kept too
    const float bodyTurnDeg = std::fabs(std::atan2(s, c)) * 57.2957795f;
    const bool moving = r.speedM > 0.15f || r.w > 0.001f || r.turnDeg != 0.0f || bodyTurnDeg > 60.0f;
    if (moving) g_wbLines.emplace_back(r.t, std::move(line));
    while (!g_wbLines.empty() && r.t - g_wbLines.front().first > kWalkBackDumpS) g_wbLines.pop_front();
    if (r.w > 0.001f) { g_wbHadOn = true; g_wbOffAt = -1.0; }
    else if (g_wbHadOn && g_wbOffAt < 0.0) g_wbOffAt = r.t;
    if (bodyTurnDeg > 90.0f && r.speedM <= 0.15f) { if (g_wbTurnSince < 0.0) g_wbTurnSince = r.t; }
    else g_wbTurnSince = -1.0;
    const bool turnedStanding = g_wbTurnSince >= 0.0 && r.t - g_wbTurnSince > 3.0 && r.t - g_wbTurnDumpAt > 10.0;
    if (turnedStanding || (g_wbHadOn && g_wbOffAt >= 0.0 && r.t - g_wbOffAt > 0.7)) {   // a walk back over, or 3 s standing turned round: its seconds out once
        if (turnedStanding) { g_wbTurnDumpAt = r.t; g_wbTurnSince = -1.0; }
        else { g_wbHadOn = false; g_wbOffAt = -1.0; }
        if (g_wbDumps >= kWalkBackDumpsAtMost) { g_wbLines.clear(); return; }
        std::string text = "# walk back " + std::to_string(++g_wbDumps) + (turnedStanding ? " (standing, the body turned round from the game's facing)" : "") + "\n";
        for (const auto& l : g_wbLines) { text += l.second; text += '\n'; }
        g_wbLines.clear();
        AcquireSRWLockExclusive(&g_wbLock);
        g_wbReady += text;
        ReleaseSRWLockExclusive(&g_wbLock);
    }
}
std::string WalkBackNoteIn() {
    const unsigned poses = g_walkBackPoses.exchange(0), full = g_walkBackFull.exchange(0), fail = g_walkBackKneeFail.exchange(0);
    const unsigned played = g_walkBackPlayed.exchange(0), jumps = g_walkBackJumps.exchange(0);
    const unsigned seen = g_walkBackSeen.exchange(0), slow = g_walkBackSlow.exchange(0), other = g_walkBackOtherMode.exchange(0);
    const unsigned ahead = g_walkBackAhead.exchange(0), placeJumps = g_walkBackPlaceJumps.exchange(0), turnOut = g_walkBackTurnOut.exchange(0);
    const float turnMax = g_walkBackTurnMaxDeg.exchange(0.0f);
    if (!poses && !placeJumps && !turnOut) return "";
    char b[1400];
    snprintf(b, sizeof b, "walk back ([body] walk_back): %u poses with the legs stepping back (%u wholly, the rest crossfading) - %u the game's walk played backwards "
             "(walk_back_reverse; %.1f s of his walk kept, %u jumps through it), %u mirrored (knees not solved %u); the travel %.0f deg from the facing at the last; "
             "of %u poses while he went, %u too slow to count as walking and %u not in the game's walk mode (the last such mode %d)",
             poses, full, played, g_walkBackKeptS.load(), jumps, poses - played, fail, g_walkBackAngle.load(), seen, slow, other, g_walkBackMode.load());
    const size_t at = strlen(b);
    snprintf(b + at, sizeof b - at, "; %u of them while he went ahead (travel under 90 deg from the facing: the hold and crossfade after a turn back); his place "
             "jumped %u times (an area change: no travel read from it); the game's own turn on the skeleton's anim bone taken out in %u poses (at most %.0f deg)",
             ahead, placeJumps, turnOut, turnMax);
    const size_t at2 = strlen(b);
    snprintf(b + at2, sizeof b - at2, "; came on wholly at once (on the game's own pose, no crossfade) %u times; a standing mode while he went let pass %u times "
             "(not held %.1f s)", g_walkBackWhole.exchange(0), g_walkBackModeDips.exchange(0), walkback::ModeGate::kStandS);
    return b;
}

// Before the game poses a rig again: were the feet we wrote last still in its bones (kept: what we wrote is what was there to be drawn), or written over?
void CheckLegWrite(uintptr_t self) {
    AcquireSRWLockShared(&g_legsLock);
    const auto it = g_legWrites.find(self);
    const bool have = it != g_legWrites.end() && GetTickCount64() - it->second.at < 500;
    const LegWrite w = have ? it->second : LegWrite{};
    ReleaseSRWLockShared(&g_legsLock);
    if (!have) return;
    uintptr_t world = 0, mats = 0;
    int32_t n = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &n) || !Get(world + 4, &mats)) return;
    bool kept = true;
    for (int side = 0; side < 2; ++side) {
        float m[16];
        if (w.foot[side] < 0 || w.foot[side] >= n || !SafeCopy(m, (const void*)(mats + (uintptr_t)w.foot[side] * 64), sizeof m)) return;
        for (int i = 0; i < 3; ++i) kept = kept && std::fabs(m[12 + i] - w.pos[side][i]) < 1e-3f;
    }
    AcquireSRWLockExclusive(&g_legsLock);
    (kept ? g_legsDiag.kept : g_legsDiag.lost) += 1;
    ReleaseSRWLockExclusive(&g_legsLock);
}
int g_nRemote = 0;
ULONGLONG g_remoteAt = 0;
std::atomic<unsigned> g_remotePosed{0}, g_remoteUnmatched{0}, g_remoteUnposable{0};
std::atomic<float> g_heroYawInModel{0.0f};   // the camera's yaw in our own hero's model space, as his arms were last posed (HeroYawInModel)
std::atomic<ULONGLONG> g_heroYawAt{0};

// The rig of a player who is not us: if a target's hero stands where it does, its upper body turned to the look (PoseBody's turn about spine_01), each arm
// to its controller (Elbow / AimChain, the wrist as the controller holds it: PlanWrist / ApplyWrist - the weapon in hand comes along, it hangs under
// the wrist), the head as the headset is held. `base`: our own Input, for the wrist's settings only.
bool PoseRemoteHero(uintptr_t self, const Hero& h, const Input& base) {
    remotepose::Target t[remotepose::kMaxHeroes];
    int nt = 0;
    AcquireSRWLockShared(&g_remoteLock);
    if (g_nRemote > 0 && GetTickCount64() - g_remoteAt <= 300) {
        nt = g_nRemote;
        for (int i = 0; i < nt; ++i) t[i] = g_remote[i];
    }
    ReleaseSRWLockShared(&g_remoteLock);
    if (!nt) return false;
    float x = 0.0f, z = 0.0f;
    Mat M{};
    const bool placed = ecs::BodyPlace(self, &x, &z, nullptr, 500, M.m);
    const int at = placed ? remotepose::Match(x, z, t, nt) : -1;
    if (at < 0) { g_remoteUnmatched.fetch_add(1, std::memory_order_relaxed); return false; }
    const remotepose::Target& r = t[at];
    if (!h.bindOk || h.head < 0 || h.spine < 0) { g_remoteUnposable.fetch_add(1, std::memory_order_relaxed); return false; }
    uintptr_t world = 0, mats = 0;
    int32_t n = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &n) || !Get(world + 4, &mats) || n != h.count) return false;
    std::vector<Mat> oldW(n);
    if (!SafeCopy(oldW.data(), (const void*)mats, sizeof(Mat) * n)) return false;
    std::vector<Mat> newW = oldW;
    // the look in THIS rig's model, from the pose's world heading through the rig's matrix now (never against a facing the sender's game had and ours has not)
    float yawModel = r.yawInModel;
    if (placed && remotepose::ModelYawOfHeading(M.m, r.heading, &yawModel)) g_remoteYawFromWorld.fetch_add(1, std::memory_order_relaxed);
    else yawModel = r.yawInModel;
    const float c = cosf(yawModel), s = sinf(yawModel);
    // [multiplayer] pose_share_legs: the whole body to the look (PoseBody's bodyTurn 2, about the model's origin - where he stands) and the legs stepping along his
    // travel; else the upper body alone (spine_01 and up about the spine's own vertical, bodyTurn 1), the legs the game's
    const bool legs = r.legs && gaitlib::Loaded() && h.leg[0][2] >= 0 && h.leg[1][2] >= 0;
    if (legs) {
        Mat T{}; T.m[0] = c; T.m[2] = -s; T.m[5] = 1; T.m[8] = s; T.m[10] = c; T.m[15] = 1;
        for (int i = 0; i < n; ++i) newW[i] = Mul(newW[i], T);
        RemoteLegs(self, h, &r, x, z, c, s, T, &M, newW, oldW);
    } else {   // (the trunk at most remotepose::kMaxTwistDeg over the hips, the rest of the look turning him whole: walking back, the game faces him down his path, and the
               // trunk alone turned half a turn over the game's hips hung his left leg from its right - crossed legs)
        float bodyYaw = 0.0f, upperYaw = 0.0f;
        remotepose::TurnSplit(yawModel, remotepose::kMaxTwistDeg * 0.017453292f, &bodyYaw, &upperYaw);
        const float cb = cosf(bodyYaw), sb = sinf(bodyYaw), cu = cosf(upperYaw), su = sinf(upperYaw);
        Mat B{}; B.m[0] = cb; B.m[2] = -sb; B.m[5] = 1; B.m[8] = sb; B.m[10] = cb; B.m[15] = 1;
        if (bodyYaw != 0.0f) for (int i = 0; i < n; ++i) newW[i] = Mul(newW[i], B);
        Mat U{}; U.m[0] = cu; U.m[2] = -su; U.m[5] = 1; U.m[8] = su; U.m[10] = cu; U.m[15] = 1;
        const V3 p = Pos(newW[h.spine]);
        U.m[12] = p.x - (p.x * cu + p.z * su); U.m[14] = p.z - (-p.x * su + p.z * cu);
        for (int i = 0; i < n; ++i) if (h.upper[i]) newW[i] = Mul(newW[i], U);
    }
    const std::vector<Mat> baseW = newW;
    const V3 head = Pos(newW[h.head]);
    float headP[3] = {head.x, head.y, head.z};
    // [format 6] his eye on this rig (its head bone less his head bone from his eye) and his model's units a metre: every offset from his eye lands where it is
    // from his head bone - the same hero's rig, in his own units (his eye is not this rig's head bone, nor his scale the head bone's height over his eye height)
    const float upm = r.drawn ? r.modelUpm : remotepose::ModelUpm(head.y, r.eyeM);
    if (r.drawn) {
        const float hb[3] = {head.x, head.y, head.z};
        remotepose::EyeOnRig(hb, r, c, s, upm, headP);
    }
    Input in = base;
    for (int side = 0; side < 2; ++side) memcpy(in.rot[side], r.rot[side], sizeof in.rot[side]);
    // [format 5] each weapon slot's attach bone as the sender draws it (the game draws the item on it), with the bone's own size as the game has it here (a slot
    // the game hides here - a point - borrows the other slot's size, else it is left as it is)
    auto rowLens = [&](const Mat& m, float len[3]) { for (int i = 0; i < 3; ++i) len[i] = Len(V3{m.m[i * 4], m.m[i * 4 + 1], m.m[i * 4 + 2]}); return std::min(len[0], std::min(len[1], len[2])); };
    Mat attachW[2];
    bool have[2] = {false, false};
    for (int slot = 0; slot < 2; ++slot) {
        const int wb = h.weapon[slot];
        if (!r.attach[slot] || wb < 0) continue;
        float len[3];
        if (rowLens(newW[wb], len) < 0.3f) {
            const int ob = h.weapon[slot ^ 1];
            if (ob < 0 || rowLens(newW[ob], len) < 0.3f) continue;
        }
        float o[3], rows[3][3];
        remotepose::AttachInModel(r, slot, headP, c, s, upm, o, rows);
        Mat& m = attachW[slot];
        m = Mat{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) m.m[i * 4 + j] = rows[i][j] * len[i];
        m.m[12] = o[0]; m.m[13] = o[1]; m.m[14] = o[2]; m.m[15] = 1.0f;
        have[slot] = true;
    }
    bool carried[2] = {false, false};   // a slot's bone already where the weapon says (carried by the hand that holds it)
    for (int side = 0; side < 2; ++side) {
        const int u = h.upr[side], l = h.lwr[side], k = h.hand[side];
        if (r.drawn && u >= 0 && l >= 0 && k >= 0) {
            // [format 6] the hand as he draws it - its place and turn - the elbow out his way (toward his elbow as drawn), the weapon's bones put below from
            // his too: the weapon sits in the hand as in his ("the weapon not in the hand the way the owner holds it", 2026-10-26 - the hand had been put by
            // THIS game's grip of the frame, and his is not: a grip taken calm, a staff's own hold, the weight model's hand fitted on the weapon)
            float o[3], rows[3][3], el[3];
            remotepose::BoneInModel(r.handPos[side], r.handQ[side], r.handMirror[side], headP, c, s, upm, o, rows);
            remotepose::HandTarget(headP, r.handElbow[side], c, s, upm, el);
            V3 T = {o[0], o[1], o[2]};
            const V3 S = Pos(newW[u]);
            const float L1 = Len(Sub(Pos(newW[l]), S)), L2 = Len(Sub(Pos(newW[k]), Pos(newW[l])));
            V3 rest = {side == 0 ? -0.5f : 0.5f, -1.0f, -0.3f};   // (Reach's, for an elbow on the line from the shoulder to the hand)
            rest = {rest.x * c + rest.z * s, rest.y, -rest.x * s + rest.z * c};
            const V3 pole = Add(Sub(V3{el[0], el[1], el[2]}, S), Scale(rest, 0.01f * (L1 + L2)));
            const V3 E = Elbow(S, L1, L2, T, pole);
            AimChain(h, u, l, k, E, T, baseW, newW);
            const V3 reached = Pos(newW[k]);
            float len[3];
            rowLens(newW[k], len);
            Mat hm = newW[k];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) hm.m[i * 4 + j] = rows[i][j] * len[i];
            auto det3 = [](const Mat& m) { return Dot(V3{m.m[0], m.m[1], m.m[2]}, Cross(V3{m.m[4], m.m[5], m.m[6]}, V3{m.m[8], m.m[9], m.m[10]})); };
            if ((det3(hm) < 0.0f) != (det3(newW[k]) < 0.0f)) for (int j = 0; j < 3; ++j) hm.m[j] = -hm.m[j];   // (this rig's handedness of the bone kept)
            hm.m[12] = reached.x; hm.m[13] = reached.y; hm.m[14] = reached.z;   // (out of the arm's reach: at the arm's end, turned as his)
            newW[k] = hm;
            std::vector<char> fromK(n, 0);
            fromK[k] = 1;
            Propagate(h, baseW, newW, fromK, k);
            const float missCm = Len(Sub(reached, T)) * 100.0f / std::max(1.0f, upm);
            g_remoteHandsDrawn.fetch_add(1, std::memory_order_relaxed);
            for (unsigned was = g_remoteReachMissMm.load(std::memory_order_relaxed); missCm * 10.0f > (float)was &&
                 !g_remoteReachMissMm.compare_exchange_weak(was, (unsigned)(missCm * 10.0f), std::memory_order_relaxed);) {}
            continue;
        }
        if (u < 0 || l < 0 || k < 0 || !(r.flags & (side ? poseshare::kLeft : poseshare::kRight))) continue;
        // the hand that holds the weapon of its own slot goes where the weapon says - by the game's own grip of that hand this frame (its attach bone against
        // it), when the game shows the weapon on this wrist now (format 5: no hands as drawn)
        const int wb = h.weapon[side];
        if (r.holds[side] && have[side] && wb >= 0) {
            const Mat rel = Mul(oldW[wb], InvAffine(oldW[k]));
            float rl[3];
            if (rowLens(rel, rl) > 0.3f) {
                const Mat handW = Mul(InvAffine(rel), attachW[side]);
                V3 T = Pos(handW);
                const V3 S = Pos(newW[u]);
                const float L1 = Len(Sub(Pos(newW[l]), S)), L2 = Len(Sub(Pos(newW[k]), Pos(newW[l])));
                V3 pole = {side == 0 ? -0.5f : 0.5f, -1.0f, -0.3f};
                pole = {pole.x * c + pole.z * s, pole.y, -pole.x * s + pole.z * c};
                const V3 E = Elbow(S, L1, L2, T, pole);
                AimChain(h, u, l, k, E, T, baseW, newW);
                const V3 reached = Pos(newW[k]);
                newW[k] = handW;
                newW[k].m[12] = reached.x; newW[k].m[13] = reached.y; newW[k].m[14] = reached.z;   // (out of the arm's reach: the hand at the arm's end, turned as the weapon says)
                std::vector<char> fromK(n, 0);
                fromK[k] = 1;
                Propagate(h, baseW, newW, fromK, k);
                carried[side] = Len(Sub(reached, Pos(handW))) < 1e-3f * std::max(1.0f, upm);
                continue;
            }
        }
        float tp[3];
        remotepose::HandTarget(headP, r.hand[side], c, s, upm, tp);
        V3 T = {tp[0], tp[1], tp[2]};
        const Wrist wr = PlanWrist(h, side, baseW, in, c, s);
        if (wr.ok) T = Sub(T, Scale(wr.point, 0.5f * wr.reach));   // the grip is the palm's middle, the wrist half a hand behind it
        const V3 S = Pos(newW[u]);
        const float L1 = Len(Sub(Pos(newW[l]), S)), L2 = Len(Sub(Pos(newW[k]), Pos(newW[l])));
        V3 pole = {side == 0 ? -0.5f : 0.5f, -1.0f, -0.3f};   // Reach's: elbow down, a little out and back - of the body as turned
        pole = {pole.x * c + pole.z * s, pole.y, -pole.x * s + pole.z * c};
        const V3 E = Elbow(S, L1, L2, T, pole);
        AimChain(h, u, l, k, E, T, baseW, newW);
        if (wr.ok) {
            ApplyWrist(newW[k], wr);
            std::vector<char> fromK(n, 0);
            fromK[k] = 1;
            Propagate(h, baseW, newW, fromK, k);
        }
    }
    if (r.flags & poseshare::kHead) {   // the head as the headset is held: its bind forward and top, as posed now, onto the look's
        float fwd[3], up[3];
        if (remotepose::LookFrame(r.headQ, c, s, remotepose::kMaxPitchDeg, fwd, up)) {
            const Mat inv = InvAffine(h.bind[h.head]);
            const V3 lf = Norm(RotOnly({0.0f, 0.0f, 1.0f}, inv)), lu = Norm(RotOnly({0.0f, 1.0f, 0.0f}, inv));
            const V3 f0 = Norm(RotOnly(lf, newW[h.head])), u0 = Norm(RotOnly(lu, newW[h.head]));
            const float a0[3] = {f0.x, f0.y, f0.z}, b0[3] = {u0.x, u0.y, u0.z};
            float R9[9];
            if (remotepose::FrameTurn(a0, b0, fwd, up, R9)) {
                Mat R{};
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) R.m[i * 4 + j] = R9[i * 3 + j];
                R.m[15] = 1.0f;
                const std::vector<Mat> before = newW;
                TurnAboutSelf(newW[h.head], R);
                std::vector<char> fromH(n, 0);
                fromH[h.head] = 1;
                Propagate(h, before, newW, fromH, h.head);
            }
        }
    }
    // the weapon's slots no holding hand carried there (let go: falling, lying, on the back - or the game hangs it on the other wrist this frame): put alone
    auto put = [&](int bone, const Mat& w) {
        const std::vector<Mat> before = newW;
        newW[bone] = w;
        std::vector<char> from(n, 0);
        from[bone] = 1;
        Propagate(h, before, newW, from, bone);
    };
    for (int slot = 0; slot < 2; ++slot) {
        if (!have[slot] || carried[slot] || h.weapon[slot] < 0) continue;
        put(h.weapon[slot], attachW[slot]);
        g_remoteWeapon.fetch_add(1, std::memory_order_relaxed);
    }
    for (int slot = 0; slot < 2; ++slot) if (have[slot] && carried[slot]) g_remoteWeapon.fetch_add(1, std::memory_order_relaxed);
    // ... and the bones the game hangs the same weapon on in some animations - the other wrist's (a two-handed weapon running) and the body's two-hand bone
    // (the barbarian walking with a great axe): where the weapon's slot is, so the game cannot draw it now in the hand, now along the arm ("now on one side of
    // the hand, now on the other", the 2026-10-26 test). A mirrored bone (the left one) keeps its handedness: the copy's x row turned over (an item drawn from a
    // mirror-less matrix is inside out and culled - PoseBody's bothAttach).
    {
        const int from = have[0] ? 0 : have[1] ? 1 : -1;
        auto det = [](const Mat& m) {
            return m.m[0] * (m.m[5] * m.m[10] - m.m[6] * m.m[9]) - m.m[1] * (m.m[4] * m.m[10] - m.m[6] * m.m[8]) + m.m[2] * (m.m[4] * m.m[9] - m.m[5] * m.m[8]);
        };
        if (from >= 0) {
            const Mat src = newW[h.weapon[from]];
            const int targets[2] = {have[from ^ 1] ? -1 : h.weapon[from ^ 1], h.twoHand};
            for (const int to : targets) {
                float tl[3];
                if (to < 0 || rowLens(newW[to], tl) < 0.3f) continue;   // (shown there now only: a hidden bone stays hidden)
                Mat w = src;
                if ((det(newW[to]) < 0.0f) != (det(src) < 0.0f)) for (int j = 0; j < 3; ++j) w.m[j] = -w.m[j];
                put(to, w);
            }
        }
    }
    if (r.grips) {   // [format 5] the fingers closed as hard as his hands grip (the class's own open and closed fists)
        const int cls = h.cls >= 0 ? h.cls : 3;
        for (int side = 0; side < 2; ++side)
            if (r.flags & (side ? poseshare::kLeft : poseshare::kRight)) Fist(h, side, r.grip[side], in.fistClose[cls], in.fistOpen[cls], oldW, newW);
    }
    for (int i = 0; i < n; ++i)   // a bone that is not a number keeps the game's own pose (as PoseBody)
        for (int j = 0; j < 16; ++j)
            if (!std::isfinite(newW[i].m[j])) { newW[i] = oldW[i]; break; }
    SafeWrite((void*)mats, newW.data(), sizeof(Mat) * n);
    g_remotePosed.fetch_add(1, std::memory_order_relaxed);
    if (placed) {   // [multiplayer] pose_share_held: his hands as drawn here, in the world - what his hands have is hung on them (RemoteHandWorld)
        RemoteHands rh;
        rh.unit = r.unit;
        rh.at = GetTickCount64();
        for (int side = 0; side < 2; ++side) {
            if (h.hand[side] < 0) continue;
            const Mat w = Mul(newW[h.hand[side]], M);
            memcpy(rh.m[side], w.m, sizeof rh.m[side]);
            rh.ok[side] = boneguard::Finite(w.m);
        }
        AcquireSRWLockExclusive(&g_remoteHandsLock);
        int slot = -1, oldest = 0;
        for (int i = 0; i < remotepose::kMaxHeroes; ++i) {
            if (g_remoteHands[i].unit == r.unit) { slot = i; break; }
            if (g_remoteHands[i].at < g_remoteHands[oldest].at) oldest = i;
        }
        g_remoteHands[slot >= 0 ? slot : oldest] = rh;
        ReleaseSRWLockExclusive(&g_remoteHandsLock);
    }
    return true;
}

// [body] gait_legs_third / gait_legs_f4: our own hero's legs by the gait, after PoseBody turned his whole body to the camera (yaw: the camera's yaw in his
// model, PoseBody's own) - the steps along his real travel against the way he now faces, the knees ahead, the feet flat facing the body.
std::atomic<unsigned> g_ownLegs{0};
void PoseOwnLegs(uintptr_t self, const Hero& h, float yaw, float eyeM) {
    if (!gaitlib::Loaded() || !h.bindOk || h.head < 0 || h.leg[0][2] < 0 || h.leg[1][2] < 0 || !std::isfinite(yaw)) return;
    float x = 0.0f, z = 0.0f;
    Mat M{};
    if (!ecs::BodyPlace(self, &x, &z, nullptr, 500, M.m)) return;
    uintptr_t world = 0, mats = 0;
    int32_t n = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &n) || !Get(world + 4, &mats) || n != h.count) return;
    std::vector<Mat> oldW(n);
    if (!SafeCopy(oldW.data(), (const void*)mats, sizeof(Mat) * n)) return;
    std::vector<Mat> newW = oldW;
    const float c = cosf(yaw), s = sinf(yaw);
    Mat T{}; T.m[0] = c; T.m[2] = -s; T.m[5] = 1; T.m[8] = s; T.m[10] = c; T.m[15] = 1;   // (the turn PoseBody gave the body: the feet's rest turned with it)
    remotepose::Target own;
    own.eyeM = eyeM > 0.5f ? eyeM : 1.7f;
    RemoteLegs(self, h, &own, x, z, c, s, T, &M, newW, oldW);   // (own: the eye height only - the travel through the rig's matrix)
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < 16; ++j)
            if (!std::isfinite(newW[i].m[j])) { newW[i] = oldW[i]; break; }
    SafeWrite((void*)mats, newW.data(), sizeof(Mat) * n);
    g_ownLegs.fetch_add(1, std::memory_order_relaxed);
}

// A rig lying down (a corpse, a fall): its head under 35 % of its standing height, as OursNow tells it. Not read: not down.
bool HeadDown(uintptr_t self, const Hero& h) {
    if (h.head < 0 || h.head >= (int)h.bind.size()) return false;
    const float standing = h.bind[h.head].m[13];
    uintptr_t world = 0, mats = 0;
    float head[16];
    if (!(standing > 2.0f) || !Get(self + 0x20, &world) || !Get(world + 4, &mats) || !SafeCopy(head, (const void*)(mats + (uintptr_t)h.head * 64), sizeof head)) return false;
    return !(std::isfinite(head[13]) && head[13] > 0.35f * standing);
}

// [multiplayer] pose_share_legs, a player who sends no pose (flat, or before his pose comes): his legs step along his travel all the same - his own facing
// (the game's), the travel from his rig's place through its own matrix.
std::atomic<bool> g_otherLegs{false};   // SetOtherHeroes (vrcam)
std::atomic<bool> g_selfLegs{false};    // [debug] pose_share_legs_self: our own hero's legs by the same gait (a one-PC test of the legs; SetSelfLegs)
std::atomic<unsigned> g_otherLegsNoPose{0};
void PoseLegsOnly(uintptr_t self, const Hero& h) {
    if (!gaitlib::Loaded() || !h.bindOk || h.head < 0 || h.leg[0][2] < 0 || h.leg[1][2] < 0) return;
    float x = 0.0f, z = 0.0f;
    Mat M{};
    if (!ecs::BodyPlace(self, &x, &z, nullptr, 500, M.m)) return;
    uintptr_t world = 0, mats = 0;
    int32_t n = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &n) || !Get(world + 4, &mats) || n != h.count) return;
    std::vector<Mat> oldW(n);
    if (!SafeCopy(oldW.data(), (const void*)mats, sizeof(Mat) * n)) return;
    std::vector<Mat> newW = oldW;
    // his facing as the game turns him (the model's forward in the world: its matrix's z row), followed at 6 rad a second - walking back the game turns him round
    // to his path at once, his drawn body comes round over half a second; the whole body turned by what is still to go
    float yaw = 0.0f;
    {
        const ULONGLONG tick = GetTickCount64();
        AcquireSRWLockExclusive(&g_legsLock);
        LegsMem& m = g_legsMem[self];
        const float dt = m.faceAt && tick > m.faceAt ? std::min(0.25f, (float)(tick - m.faceAt) * 0.001f) : 0.0f;
        if (!m.faceAt || tick - m.faceAt > 500) { m.faceX = m.faceZ = 0.0f; }
        remotepose::TurnToward(&m.faceX, &m.faceZ, M.m[8], M.m[10], 6.0f * dt);
        m.faceAt = tick;
        const float fx = m.faceX, fz = m.faceZ;
        ReleaseSRWLockExclusive(&g_legsLock);
        if (!remotepose::ModelYawOfDir(M.m, fx, fz, &yaw)) yaw = 0.0f;
    }
    const float c = cosf(yaw), s = sinf(yaw);
    Mat T{}; T.m[0] = c; T.m[2] = -s; T.m[5] = 1; T.m[8] = s; T.m[10] = c; T.m[15] = 1;
    if (yaw != 0.0f) for (int i = 0; i < n; ++i) newW[i] = Mul(newW[i], T);
    RemoteLegs(self, h, nullptr, x, z, c, s, T, &M, newW, oldW);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < 16; ++j)
            if (!std::isfinite(newW[i].m[j])) { newW[i] = oldW[i]; break; }
    SafeWrite((void*)mats, newW.data(), sizeof(Mat) * n);
    g_otherLegsNoPose.fetch_add(1, std::memory_order_relaxed);
}

#endif

void Arms(uintptr_t self) {
    Input in;
    AcquireSRWLockShared(&g_inLock); in = g_in; ReleaseSRWLockShared(&g_inLock);
    // The hero is looked for even while nothing is done to him: his SkeletonInstance is what
    // vrcam's facing is found from (HeroTransform), and that is needed before the body can turn.
    const bool active = !(in.mode == 0 && !in.hideHead && !in.lockUpper && !(in.bodyTurn > 0 && in.haveYaw));
    if (!active && !g_trackHero.load()) return;
    uintptr_t modelInst = 0, skel = 0;
    if (!Get(self + 0x08, &modelInst) || !Get(modelInst + 0x08, &skel)) return;
    const auto key = std::make_pair(self, skel);
    std::shared_ptr<const Hero> hero; bool have = false;
    AcquireSRWLockShared(&g_heroLock);
    auto it = g_heroes.find(key);
    if (it != g_heroes.end()) { hero = it->second; have = true; }
    // [hands] doll_rebuild: a player's rig met before its bones or its bind pose read (a model being made at a swap) is learned again, every 100 ms for 5 s
    bool again = false;
    int tries = 0;
    ULONGLONG firstMet = 0;
    const ULONGLONG now = GetTickCount64();
    if (have && (!hero || !hero->bindOk) && in.dollRebuild)
        if (const auto r = g_heroRetry.find(key); r != g_heroRetry.end() && now - r->second.first < 5000 && now - r->second.last >= 100) {
            again = true; tries = r->second.tries + 1; firstMet = r->second.first;
        }
    ReleaseSRWLockShared(&g_heroLock);
    if (!have || again) {
        auto h = std::make_shared<Hero>();
        bool player = false;
        const bool learned = Learn(self, h.get(), &player) && h->count > 0;
        if (learned && (!hero || h->bindOk)) hero = std::move(h);   // (a learned rig without its bind pose is not put back for one that has it)
        if (!learned && player && in.dollTrace && !again)   // [debug] doll_trace: a player's model that is not learned is not the doll (looked at again for 5 s)
            NoteDoll("doll: a player model whose rig is not learned (%s, %d bones): its arm / head / spine bones are not found by name or do not read%s", ModelOf(self).c_str(),
                     h->count, in.dollRebuild ? " - learned again every 100 ms for 5 s" : " - never posed as the doll");
        if (again && in.dollTrace && learned)
            NoteDoll("doll: rig %p learned on its try %d, %llu ms after it was met (its bind pose %s)", (void*)self, tries + 1, (unsigned long long)(now - firstMet),
                     hero->bindOk ? "read" : "not read yet");
        AcquireSRWLockExclusive(&g_heroLock);
        // full: the rigs that are not a hero's go first (bearstonem/d2r-pcvr's cache keeps its crossbows so), a learned hero is not read again for them
        if (g_heroes.size() > 4096) std::erase_if(g_heroes, [](const auto& e) { return !e.second; });
        if (g_heroes.size() > 4096) g_heroes.clear();
        g_heroes[key] = hero;
        if (player && (!hero || !hero->bindOk)) {
            if (g_heroRetry.size() > 256) g_heroRetry.clear();
            HeroRetry& r = g_heroRetry[key];
            if (!r.first) r.first = now;
            r.last = now; r.tries = tries;
        } else {
            g_heroRetry.erase(key);
        }
        ReleaseSRWLockExclusive(&g_heroLock);
    }
    if (!hero) return;   // not a hero
    char why[320] = "";
    const Doll doll = OursNow(self, *hero, in.dollTrace || in.mode == 2 ? why : nullptr, sizeof why, in.dollTrace);
    if (doll == Doll::Not) {   // another player, our corpse
#if D2RVR_FIRST_PERSON
        // [multiplayer] pose_share_hero: another player posed from his pose (none unless asked); with none, his legs alone ([multiplayer] pose_share_legs). Not a corpse
        // (its head down, OursNow's first test). (His body for our weapon is hit by touch's: melee_touch.cpp PublishBodies, [multiplayer] pose_share_bodies.)
        const bool down = HeadDown(self, *hero);
        if (!down) {
            if (!PoseRemoteHero(self, *hero, in) && g_otherLegs.load(std::memory_order_relaxed)) PoseLegsOnly(self, *hero);
        }
#endif
        // The arms are ours on every pose of the hero's rig, whatever the game animates (a leap, a knockback, Whirlwind, a cast, a hit): no
        // animation mode gives them back. The one way the game's pose comes back is here - the rig posed as ours not taken this time (its head
        // under 35 % of standing: lying, a corpse, a fall; too far from the look-at; another rig taken). VR F4 (arms on the controllers): said,
        // with why, at most every 5 s, without [debug] doll_trace too (which says more, below).
        static std::atomic<ULONGLONG> armsToldAt{0};
        if (!in.dollTrace && in.mode == 2 && self == g_heroSelf.load() && GetTickCount64() - armsToldAt.load() > 5000) {
            armsToldAt.store(GetTickCount64());
            NoteDoll("arms: the game's own pose of the hero this time, the arms off the controllers - his rig was not taken as ours: %s", why);
        }
        // [debug] doll_trace: the rig posed as ours last turned away - or, with none ours for 150 ms, the one met - once until one is taken again
        if (in.dollTrace && (self == g_heroSelf.load() || GetTickCount64() - g_heroSeen.load() > 150) && !g_dollLetGo.exchange(true)) {
            g_dollLetGoAt.store(GetTickCount64());
            const uintptr_t last = g_heroSelf.load();
            NoteDoll("doll: let go - the hero's rig %p%s is not taken as ours: %s; instead: %s", (void*)self, self == last ? " (posed as ours last)" : "", why,
                     self == last ? "the game's own pose of it (no arms on the controllers, the head shown), the facing kept from the last rig" :
                     "no rig is ours (the one posed last is not posed any more: the next line)");
            if (self != last) NoteDoll("doll: (the rig posed as ours last: %p, %llu ms ago)", (void*)last, (unsigned long long)(GetTickCount64() - g_heroSeen.load()));
        }
        return;
    }
    if (doll == Doll::Along) {   // posed with ours, not taken: the facing stays on the rig taken, nothing is learned from this one's pose
        in.along = true;
        if (in.dollTrace && g_dollAlongTold.exchange(self) != self) NoteDoll("doll: rig %p posed along with the hero's, not taken: %s", (void*)self, why);
    } else {
        if (in.dollTrace && g_dollLetGo.exchange(false))
            NoteDoll("doll: taken again after %llu ms - rig %p%s", (unsigned long long)(GetTickCount64() - g_dollLetGoAt.load()), (void*)self,
                     self == g_heroSelf.load() ? " (the same rig)" : " (a new rig: the model rebuilt - a weapon swap, an area, a load - or another one nearer the look-at)");
        NoteHero(self);
        // the hero's class, from OUR rig only (the per-class wrist depth and D2R VR Settings read it): Learn met every player's rig and a second player's Amazon
        // took our class for hers (the 2026-10-26 two-PC test's log learned amazon, barbarian, druid, necromancer rigs)
        if (hero->cls >= 0) g_heroClass.store(hero->cls);
    }
#if D2RVR_FIRST_PERSON
    const bool selfLegs = doll == Doll::Ours && g_selfLegs.load(std::memory_order_relaxed);   // [debug] pose_share_legs_self: after our own pose, if any
    if (!active) { if (selfLegs) PoseLegsOnly(self, *hero); return; }
#else
    if (!active) return;
#endif
    // The facing of this very frame, not of the last 10 ms tick - the hero only.
    if (in.haveYaw) if (const FreshFn f = g_fresh.load()) {
        float yaw = 0.0f, eye[3] = {}; bool eyeOk = false;
        if (f(&yaw, eye, &eyeOk)) {
            in.yawInModel = yaw;
#if D2RVR_FIRST_PERSON
            if (doll == Doll::Ours && in.mode == 2) { g_heroYawInModel.store(yaw); g_heroYawAt.store(GetTickCount64()); }   // [multiplayer] pose_share_hero: for the others
#endif
            if (eyeOk) { memcpy(in.eyeModel, eye, sizeof eye); in.haveEye = true; }
        }
    }
    in.roomShiftOn = false;   // [openxr] roomscale_body: from this pose's matrix only (the fresh facing above read it)
    if (in.haveYaw) if (const RoomShiftFn f = g_freshShift.load()) in.roomShiftOn = f(in.roomShift);
    if (in.mode == 2) if (const FreshHandsFn f = g_freshHands.load()) f(&in);
    // [hands] shoulder_draw, the shield: which arm it is on (or put away), stepped once per head sample, before the weight model asks which hand is busy with it
    shieldarmwire::Apply(&in);
    // [hands] shoulder_draw_ranged: a bow or crossbow put away behind a shoulder (stepped once per head sample). While it is away nothing is posed round it: no bow aim, string, rig or
    // hand pose, no staff or gun - the hands are free; it is hidden at the end of the pose (HideRanged).
    rangedwire::Apply(&in);
    if (in.rangedStowed) {
        in.staff = false;
        in.gunStock = false;
        in.bowAim = in.bowAxis = in.bowRest = in.bowRig = in.bowString = in.bowStave = in.bowHandPose = false;
    }
    // [weight]: the weapon stepped (once per head sample) and the hands on it moved to where it has them
    weight::Out wo;
    weight::Apply(&in, &wo);
    // [weight] grab_bodies: a hand holding a body drawn where it holds it
    if (in.mode == 2) if (const GrabHandsFn g = g_grabHands.load()) g(&in);
    PoseBody(self, *hero, in, wo);
#if D2RVR_FIRST_PERSON
    if (selfLegs) PoseLegsOnly(self, *hero);
    else if (in.gaitLegs && doll == Doll::Ours && in.bodyTurn == 2 && in.haveYaw) PoseOwnLegs(self, *hero, in.yawInModel, in.userHeadM);   // [body] gait_legs_third / _f4
#endif
}

}  // namespace

// From vrcam, every 10 ms.
int LockState() { return g_lockState.load(); }

#if D2RVR_FIRST_PERSON
void SetRemoteHeroes(const remotepose::Target* t, int n) {
    n = std::clamp(n, 0, remotepose::kMaxHeroes);
    AcquireSRWLockExclusive(&g_remoteLock);
    for (int i = 0; i < n; ++i) g_remote[i] = t[i];
    g_nRemote = t ? n : 0;
    g_remoteAt = GetTickCount64();
    ReleaseSRWLockExclusive(&g_remoteLock);
}
RemoteHeroCount TakeRemoteHeroCount() {
    RemoteHeroCount c;
    c.posed = g_remotePosed.exchange(0);
    c.unmatched = g_remoteUnmatched.exchange(0);
    c.unposable = g_remoteUnposable.exchange(0);
    c.weapon = g_remoteWeapon.exchange(0);
    c.legs = g_remoteLegs.exchange(0);
    c.legsNoPose = g_otherLegsNoPose.exchange(0);
    c.yawFromWorld = g_remoteYawFromWorld.exchange(0);
    c.handsDrawn = g_remoteHandsDrawn.exchange(0);
    c.reachMissCm = (float)g_remoteReachMissMm.exchange(0) * 0.1f;
    return c;
}
void SetOtherHeroes(bool legs) { g_otherLegs.store(legs); }
void SetSelfLegs(bool on) { g_selfLegs.store(on); }
void SetGaitCone(float halfAngleDeg) { g_gaitCone.store(std::clamp(halfAngleDeg, 0.0f, 180.0f)); }
unsigned TakeOwnLegs() { return g_ownLegs.exchange(0); }
std::string WalkBackNote() { return WalkBackNoteIn(); }
void SetWalkBackDump(bool on) { g_walkBackDumpOn.store(on); }
std::string TakeWalkBackDump() {
    AcquireSRWLockExclusive(&g_wbLock);
    std::string t;
    t.swap(g_wbReady);
    ReleaseSRWLockExclusive(&g_wbLock);
    return t;
}
std::string LegsNote() {
    AcquireSRWLockExclusive(&g_legsLock);
    const LegsDiag d = g_legsDiag;
    g_legsDiag = LegsDiag{};
    ReleaseSRWLockExclusive(&g_legsLock);
    if (!d.steps && !d.kept && !d.lost) return "";
    char b[560];
    snprintf(b, sizeof b, "legs: %u steps on %d rig(s), %u of them the game's own walk (in the cone or standing), our gait's share %.0f %%; feet put %.0f cm from the "
             "game's on average (%.0f at most), lifted up to %.0f cm, hips down up to %.0f cm; walking %.0f %% (speed %.2f m/s on average, %.2f at most); knees not "
             "solved %u; our write still there at the next pose %u, written over %u; reversals turned at once %u (refused %u)",
             d.steps, d.nRigs, d.gameWalk, d.steps ? 100.0f * d.shareSum / d.steps : 0.0f, d.feet ? d.footCmSum / d.feet : 0.0f, d.footCmMax, d.liftCmMax, d.dropCmMax,
             d.steps ? 100.0f * d.moveSum / d.steps : 0.0f, d.steps ? d.speedSum / d.steps : 0.0f, d.speedMax, d.kneeFail, d.kept, d.lost, d.reversals, d.reverseRefused);
    return b;
}
bool DrawnAttach(int slot, float origin[3], float rows[3][3], unsigned maxAgeMs, int* main) {
    if (slot < 0 || slot > 1) return false;
    AcquireSRWLockShared(&g_drawnAttachLock);
    const DrawnAttachOut d = g_drawnAttach;
    ReleaseSRWLockShared(&g_drawnAttachLock);
    if (!d.at || GetTickCount64() - d.at > maxAgeMs || !d.ok[slot]) return false;
    memcpy(origin, d.origin[slot], sizeof d.origin[slot]);
    memcpy(rows, d.rows[slot], sizeof d.rows[slot]);
    if (main) *main = d.main;
    return true;
}
bool RemoteHandWorld(uint32_t unit, int side, float m[16], unsigned maxAgeMs) {
    if (!unit || side < 0 || side > 1 || !m) return false;
    AcquireSRWLockShared(&g_remoteHandsLock);
    bool ok = false;
    for (const RemoteHands& r : g_remoteHands)
        if (r.unit == unit && r.ok[side] && GetTickCount64() - r.at <= maxAgeMs) { memcpy(m, r.m[side], 64); ok = true; break; }
    ReleaseSRWLockShared(&g_remoteHandsLock);
    return ok;
}
bool DrawnHands(DrawnHand* out, unsigned maxAgeMs) {
    if (!out) return false;
    AcquireSRWLockShared(&g_drawnAttachLock);
    const DrawnAttachOut d = g_drawnAttach;
    ReleaseSRWLockShared(&g_drawnAttachLock);
    if (!d.at || GetTickCount64() - d.at > maxAgeMs || !d.handsOk || !(d.upm > 0.0f)) return false;
    memcpy(out->headBone, d.headBone, sizeof out->headBone);
    memcpy(out->hand, d.hand, sizeof out->hand);
    memcpy(out->rows, d.handRows, sizeof out->rows);
    memcpy(out->elbow, d.elbow, sizeof out->elbow);
    out->upm = d.upm;
    return true;
}
bool HeroYawInModel(float* yaw, unsigned maxAgeMs) {
    const ULONGLONG at = g_heroYawAt.load();
    if (!yaw || !at || GetTickCount64() - at > maxAgeMs) return false;
    *yaw = g_heroYawInModel.load();
    return std::isfinite(*yaw);
}
#endif

int CrouchLegs(float* applied) {
    if (applied) *applied = g_crouchApplied.load();
    return g_crouchLegs.load();
}
bool HeroEye(float* eyeY, float* lift, float* rawLift) {
    if (GetTickCount64() - g_eyeAt.load() > 1000) return false;
    *eyeY = g_eyeY.load(); *lift = g_lift.load();
    if (rawLift) *rawLift = g_rawLift.load();
    return *eyeY > 0.0f;
}
#if D2RVR_FIRST_PERSON
bool StaffAxis(float dir[3], float* handGap, bool* leftOn) {
    AcquireSRWLockShared(&g_staffLock);
    const StaffHold st = g_staff;
    ReleaseSRWLockShared(&g_staffLock);
    if (!st.at || GetTickCount64() - st.at > 300) return false;
    if (dir) memcpy(dir, st.dir, sizeof st.dir);
    if (handGap) *handGap = st.gap;
    if (leftOn) *leftOn = st.on;
    return true;
}

int GunAxis() { return g_gunAxis.load(); }
bool XbowBarrelDebug(float a[3], float b[3], float support[3], bool* rendered) {
    AcquireSRWLockShared(&g_gunBoneLock); const XbowDebugOut g = g_xbowDebug; ReleaseSRWLockShared(&g_gunBoneLock);
    if (!g.at || GetTickCount64() - g.at > 300) return false;
    memcpy(a, g.a, sizeof g.a); memcpy(b, g.b, sizeof g.b); memcpy(support, g.p, sizeof g.p);
    if (rendered) *rendered = g.rendered;
    return true;
}
const char* XbowBarrelWhy() { return g_xbowBarrelWhy.load(); }
bool XbowPalmDebug(float p[3], float* cm) {
    AcquireSRWLockShared(&g_gunBoneLock); const XbowPalmOut g = g_xbowPalm; ReleaseSRWLockShared(&g_gunBoneLock);
    if (!g.at || GetTickCount64() - g.at > 300) return false;
    memcpy(p, g.p, sizeof g.p);
    if (cm) *cm = g.cm;
    return true;
}
bool XbowShotRay(float a[3], float b[3], bool* barrel, bool* floor) {
    AcquireSRWLockShared(&g_gunBoneLock); const XbowRayOut g = g_xbowRay; ReleaseSRWLockShared(&g_gunBoneLock);
    if (!g.at || GetTickCount64() - g.at > 300) return false;
    memcpy(a, g.a, sizeof g.a); memcpy(b, g.b, sizeof g.b);
    if (barrel) *barrel = g.barrel;
    if (floor) *floor = g.floor;
    return true;
}
void XbowRendered(uint32_t entity, uintptr_t component, uintptr_t out) { XbowRenderedIn(entity, component, out); }
std::string XbowNoteTake() { return XbowNoteTakeIn(); }
bool GrabLine(float a[3], float b[3]) {
    AcquireSRWLockShared(&g_gunBoneLock); const GrabLineOut g = g_grabLine; ReleaseSRWLockShared(&g_gunBoneLock);
    if (!g.at || GetTickCount64() - g.at > 300) return false;
    memcpy(a, g.a, sizeof g.a); memcpy(b, g.b, sizeof g.b);
    return true;
}
int GripHand(float scale[2]) {
    AcquireSRWLockShared(&g_gripLock); const Grip g = g_grip; ReleaseSRWLockShared(&g_gripLock);
    if (scale) { scale[0] = g.ok ? g.scale[0] : 0.0f; scale[1] = g.ok ? g.scale[1] : 0.0f; }
    return g.ok ? g.hand : -1;
}
bool PosedWeapon(PosedWeaponPose& out) {
    AcquireSRWLockShared(&g_posedLock); const PosedWeaponPose p = g_posed; const ULONGLONG at = g_posedAt; ReleaseSRWLockShared(&g_posedLock);
    if (!at || GetTickCount64() - at > 100 || p.heroGeneration != HeroGen()) return false;
    out = p;
    return true;
}
bool CastPoseNow(CastPose* out, unsigned maxAgeMs) {
    AcquireSRWLockShared(&g_castLock);
    const CastHandOut hand[2] = {g_castHand[0], g_castHand[1]};
    const CastWeaponOut weapon = g_castWeapon;
    ReleaseSRWLockShared(&g_castLock);
    const ULONGLONG now = GetTickCount64();
    auto fresh = [&](ULONGLONG at) { return at && now - at <= maxAgeMs; };
    *out = CastPose{};
    for (int side = 0; side < 2; ++side) {
        out->hand[side] = fresh(hand[side].at);
        memcpy(out->palm[side], hand[side].palm, sizeof out->palm[side]);
        memcpy(out->fingers[side], hand[side].fingers, sizeof out->fingers[side]);
        memcpy(out->face[side], hand[side].face, sizeof out->face[side]);
    }
    out->weapon = fresh(weapon.at);
    out->weaponSide = weapon.side;
    memcpy(out->weaponOrigin, weapon.origin, sizeof out->weaponOrigin);
    memcpy(out->weaponTip, weapon.tip, sizeof out->weaponTip);
    return out->hand[0] || out->hand[1] || out->weapon;
}
bool GunBone(float origin[3], float axes[3][3]) {
    AcquireSRWLockShared(&g_gunBoneLock); const GunBoneOut g = g_gunBone; ReleaseSRWLockShared(&g_gunBoneLock);
    if (!g.at || GetTickCount64() - g.at > 300) return false;
    memcpy(origin, g.o, sizeof g.o); memcpy(axes, g.ax, sizeof g.ax);
    return true;
}

void ResetGrip() {
    AcquireSRWLockExclusive(&g_gripLock); g_grip.ok = false; ReleaseSRWLockExclusive(&g_gripLock);
    AcquireSRWLockExclusive(&g_gripPoseLock); g_gripPose.ok = false; ReleaseSRWLockExclusive(&g_gripPoseLock);   // (the finger pose read off the game's hand goes with the grip)
}
bool BodyBelly(float belly[3]) {
    AcquireSRWLockShared(&g_armReachLock); const BellyOut o = g_belly; ReleaseSRWLockShared(&g_armReachLock);
    if (!o.at || GetTickCount64() - o.at > 500) return false;
    memcpy(belly, o.at3, sizeof o.at3);
    return true;
}
bool ArmReach(int side, float shoulder[3], float* lengthM) {
    AcquireSRWLockShared(&g_armReachLock); const ArmReachOut o = g_armReach[side & 1]; ReleaseSRWLockShared(&g_armReachLock);
    if (!o.at || GetTickCount64() - o.at > 500) return false;
    memcpy(shoulder, o.shoulder, sizeof o.shoulder);
    if (lengthM) *lengthM = o.length;
    return true;
}
bool GripMarks(int side, float grasp[3], float palm[3]) {
    AcquireSRWLockShared(&g_gripMarkLock); const GripMarkOut g = g_gripMark[side & 1]; ReleaseSRWLockShared(&g_gripMarkLock);
    if (!g.at || GetTickCount64() - g.at > 300) return false;
    memcpy(grasp, g.grasp, sizeof g.grasp); memcpy(palm, g.palm, sizeof g.palm);
    return true;
}
#endif

#if D2RVR_FIRST_PERSON
bool HeroPalmModel(int side, float palm[3]) {
    AcquireSRWLockShared(&g_forearmLock);
    const auto f = g_forearm[side & 1];
    ReleaseSRWLockShared(&g_forearmLock);
    if (!f.at || !f.palmOk || GetTickCount64() - f.at > 250) return false;
    memcpy(palm, f.palmModel, sizeof f.palmModel);
    return true;
}
bool HeroArmWorld(int side, HeroArm* out, unsigned maxAgeMs, unsigned* ageMs) {
    AcquireSRWLockShared(&g_forearmLock);
    const auto f = g_forearm[side & 1];
    ReleaseSRWLockShared(&g_forearmLock);
    if (ageMs) *ageMs = 0xFFFFFFFFu;
    if (!f.at || !f.worldOk || !f.worldAt) return false;
    const ULONGLONG age = GetTickCount64() - f.worldAt;
    if (ageMs) *ageMs = (unsigned)std::min<ULONGLONG>(age, 0xFFFFFFFEu);
    if (age > maxAgeMs) return false;
    memcpy(out->elbow, f.elbowW, sizeof out->elbow);
    memcpy(out->wrist, f.wristW, sizeof out->wrist);
    memcpy(out->across, f.acrossW, sizeof out->across);
    memcpy(out->point, f.pointW, sizeof out->point);
    memcpy(out->face, f.faceW, sizeof out->face);
    out->hand = f.handW;
    out->box = f.boxOk;
    memcpy(out->boxC, f.boxCW, sizeof out->boxC);
    memcpy(out->boxH, f.boxHW, sizeof out->boxH);
    return true;
}
const char* HeroArmWhy() { return g_armWorldWhy.load(); }
bool HeroPalm(int side, float palm[3]) {
    AcquireSRWLockShared(&g_forearmLock);
    const auto f = g_forearm[side & 1];
    ReleaseSRWLockShared(&g_forearmLock);
    if (!f.at || !f.palmOk || GetTickCount64() - f.at > 250) return false;
    memcpy(palm, f.palm, sizeof f.palm);
    return true;
}

void WantShield(bool on) { if (on) g_shieldWantedAt.store(GetTickCount64()); }
std::string ForearmTwistNote(float roll[2]) {
#if D2RVR_FIRST_PERSON
    roll[0] = g_twistOut[0].load(); roll[1] = g_twistOut[1].load();
#else
    roll[0] = roll[1] = NAN;
#endif
    AcquireSRWLockShared(&g_twistNoteLock); std::string note = g_twistNote; ReleaseSRWLockShared(&g_twistNoteLock);
    return note;
}
int ShieldStrapped() { return g_shieldStrapped.load(); }
void ShieldAdjNote(float seen[6], float* moveCm, float* turnDeg) {
    for (int q = 0; q < 6; ++q) seen[q] = g_shieldAdjSeen[q].load();
    *moveCm = g_shieldAdjMoveCm.load();
    *turnDeg = g_shieldAdjTurnDeg.load();
}
bool ShieldAttach(ShieldPose* out) {
    AcquireSRWLockShared(&g_gunBoneLock);
    const ShieldPose pose = g_shieldPose;
    const ULONGLONG at = g_shieldAt;
    ReleaseSRWLockShared(&g_gunBoneLock);
    if (!at || GetTickCount64() - at > 100 || pose.heroGeneration != HeroGen()) return false;
    *out = pose;
    return true;
}
bool HeroForearm(int side, float elbow[3], float wrist[3], float across[3]) {
    AcquireSRWLockShared(&g_forearmLock);
    const auto f = g_forearm[side & 1];
    ReleaseSRWLockShared(&g_forearmLock);
    if (!f.at || GetTickCount64() - f.at > 250) return false;
    memcpy(elbow, f.elbow, sizeof f.elbow);
    memcpy(wrist, f.wrist, sizeof f.wrist);
    memcpy(across, f.across, sizeof f.across);
    return true;
}

#endif
#if D2RVR_FIRST_PERSON
void SetBowDraw(const BowDraw& d) {
    BowDraw c = d;
    c.draw = std::isfinite(c.draw) ? std::clamp(c.draw, 0.0f, 1.0f) : 0.0f;
    const bool finite = std::isfinite(c.nock[0] + c.nock[1] + c.nock[2] + c.dir[0] + c.dir[1] + c.dir[2]);
    if (c.anchored && (!finite || c.dir[0] * c.dir[0] + c.dir[1] * c.dir[1] + c.dir[2] * c.dir[2] < 1e-4f)) c.anchored = false;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_bowLock);
    g_bowFeed = c;
    g_bowFeedAt = now;
    ReleaseSRWLockExclusive(&g_bowLock);
    if (c.drawing) g_bowDrawnAt.store(now);
}

void SetBowHandControls(int side, float trigger, float squeeze) {
    if (side != 0 && side != 1) return;
    AcquireSRWLockExclusive(&g_bowLock);
    g_bowCtlAt = GetTickCount64();
    g_bowCtlSide = side;
    g_bowCtlTrigger = trigger;
    g_bowCtlSqueeze = squeeze;
    ReleaseSRWLockExclusive(&g_bowLock);
}

std::string BowRigDiag() {
    const uint32_t n = g_bowArrowSamples.load();
    if (!n) return "the bow's arrow bone: no pose of it read yet";
    char b[160];
    snprintf(b, sizeof b, "the bow's arrow bone in the game's own pose: %.2f to %.2f of its bind size (%u poses read)", g_bowArrowLow.load(), g_bowArrowHigh.load(), n);
    return b;
}

std::string BowRigTakeNote() {
    std::string s;
    AcquireSRWLockExclusive(&g_bowNoteLock);
    s.swap(g_bowNote);
    ReleaseSRWLockExclusive(&g_bowNoteLock);
    return s;
}

namespace {
// The records of every bow skeleton the rig has met, for bow_census.h.
std::vector<bowcensus::Record> BowCensus() {
    std::vector<bowcensus::Record> out;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_bowRigLock);
    for (const auto& [key, e] : g_bowRigs) {
        if (!e.isBow || !e.seenAt || now - std::min(now, e.seenAt) > 1500) continue;
        bowcensus::Record r;
        r.instance = (uint64_t)key.first;
        r.model = e.model;
        r.bones = e.count;
        r.owner = e.owner;
        r.known = e.rig.valid;
        r.entity = e.entity;
        r.parent = e.parent;
        r.status = e.last;
        r.seenAt = e.seenAt;
        out.push_back(std::move(r));
    }
    ReleaseSRWLockExclusive(&g_bowRigLock);
    return out;
}
}  // namespace

const char* BowRigStatus() {
    const ULONGLONG now = GetTickCount64(), at = g_bowPosedAt.load();
    if (at && now - at < 1000) return "posed";
    if (const char* hero = bowcensus::HeroStatus(BowCensus(), now)) return hero;   // his own bow's, not a reason of another skeleton's
    return g_bowWhy.load();
}

std::string BowRigWho() { return bowcensus::Text(BowCensus(), GetTickCount64()); }

void FlailHit(int side) { if (side == 0 || side == 1) g_flailHits[side].fetch_add(1); }
const char* FlailStatus() { return g_flailWhy.load(); }
const char* HeadStatus() {
    const ULONGLONG seen = g_headSeenAt.load();
    if (!seen || GetTickCount64() - seen > 2000) return "no Necromancer head skeleton posed in the last 2 s - the pose hook meets none";
    return g_headWhy.load();
}
std::string ArmTakeNote() {
    std::string s;
    AcquireSRWLockExclusive(&g_armNoteLock);
    s.swap(g_armNote);
    ReleaseSRWLockExclusive(&g_armNoteLock);
    return s;
}
std::string HeadTakeNote() {
    std::string s;
    AcquireSRWLockExclusive(&g_headNoteLock);
    s.swap(g_headNote);
    ReleaseSRWLockExclusive(&g_headNoteLock);
    return s;
}
std::string DollTakeNote() {
    std::string s;
    AcquireSRWLockExclusive(&g_dollNoteLock);
    s.swap(g_dollNote);
    ReleaseSRWLockExclusive(&g_dollNoteLock);
    return s;
}
std::string FlailTakeNote() {
    std::string s;
    AcquireSRWLockExclusive(&g_flailNoteLock);
    s.swap(g_flailNote);
    ReleaseSRWLockExclusive(&g_flailNoteLock);
    return s;
}

std::string BowProbe() {
    AcquireSRWLockShared(&g_bowProbeLock);
    std::string t = g_bowProbeText;
    ReleaseSRWLockShared(&g_bowProbeLock);
    return t;
}

std::string BowHandPoseDiag() {
    const int side = g_handPoseSide.load();
    char b[300];
    snprintf(b, sizeof b, "%s%s%s%s (the weapon attach bones at the wrists: right %.1f, left %.1f)", g_handPoseWhy.load(),
             side == 0 ? " - the right hand" : side == 1 ? " - the left hand" : "", side >= 0 && g_handPoseHow.load()[0] ? ", told by " : "",
             side >= 0 ? g_handPoseHow.load() : "", g_handPoseAttach[0].load(), g_handPoseAttach[1].load());
    std::string out = b;
    // With the pose made: how far the fingers close, and from what - only once they have got there (a swing is one word, so the log
    // keeps a line for each rest, not one for each frame of it). "curl" is how bent the fingers are, 100% = straight: Fist's own, and
    // again as the pose is written, to tell a pose that something after it undid.
    if (!strcmp(g_handPoseWhy.load(), "posed")) {
        const float target = g_handPoseTarget.load(), used = g_handPoseUsed.load();
        if (std::fabs(target - used) >= 0.005f) {
            out += ", the fingers are moving";
        } else {
            const int src = g_handPoseSource.load();
            auto pct = [](float v) { return v < 0.0f ? -1 : (int)std::lround(v * 20.0f) * 5; };   // (to 5%, so a hand at rest is one line)
            const int curl = pct(g_handPoseCurl.load()), written = pct(g_handPoseCurlOut.load());
            char curlText[40], writtenText[40];
            snprintf(curlText, sizeof curlText, curl < 0 ? "not measured" : "%d%%", curl);
            snprintf(writtenText, sizeof writtenText, written < 0 ? "not measured" : "%d%%", written);
            const float game = g_handPoseGame.load();
            if (game > 0.995f)   // the string taken: the game's own draw pose, whatever the controls read
                snprintf(b, sizeof b, ", the game's own draw pose (the fingers hooked, the thumb turned in; not the fist table) by %s, the fingers' curl %s (as written: %s)",
                         bowhand::SourceName((bowhand::Source)src), curlText, writtenText);
            else
                snprintf(b, sizeof b, ", closing %d%% of a full fist (hook %d%%) by %s%s%s, the fingers' curl %s (as written: %s)",
                         pct(g_handPoseAmount.load()), pct(g_handPoseHook.load()), bowhand::SourceName((bowhand::Source)src),
                         src == (int)bowhand::Source::Trigger || src == (int)bowhand::Source::Grip ? " (read raw)" : "", game >= 0.0f ? ", the game's draw pose for the string" : "",
                         curlText, writtenText);
            out += b;
        }
    }
    return out;
}

bool BowAxis(float out[3]) {
    AcquireSRWLockShared(&g_bowAimLock);
    const bool fresh = g_aimAxisAt && GetTickCount64() - g_aimAxisAt < 300;
    if (fresh) memcpy(out, g_aimAxis, sizeof g_aimAxis);
    ReleaseSRWLockShared(&g_bowAimLock);
    return fresh;
}

bool BowAimState(float* blend, float* angleDeg, float* rawDeg, const char** why) {
    AcquireSRWLockShared(&g_bowAimLock);
    const BowAimStat st = g_aimStat;
    ReleaseSRWLockShared(&g_bowAimLock);
    const bool fresh = st.on && st.at && GetTickCount64() - st.at < 500;
    if (blend) *blend = fresh ? st.blend : 0.0f;
    if (angleDeg) *angleDeg = fresh ? st.angle : 0.0f;
    if (rawDeg) *rawDeg = fresh ? st.raw : 0.0f;
    if (why) *why = st.on ? (fresh ? st.why : "no pose of the hero in the last half second") : "off";
    return st.on;
}

bool BowRestPoint(float out[3]) {
    AcquireSRWLockShared(&g_bowAimLock);
    const bool fresh = g_restAt && GetTickCount64() - g_restAt < 300;
    if (fresh) memcpy(out, g_restHands, sizeof g_restHands);
    ReleaseSRWLockShared(&g_bowAimLock);
    return fresh;
}

std::string StringAnchorWho() {
    AcquireSRWLockShared(&g_bowAimLock);
    std::string who = g_anchorWho;
    ReleaseSRWLockShared(&g_bowAimLock);
    return who;
}

bool StringAnchorHands(float out[3]) {
    AcquireSRWLockShared(&g_bowAimLock);
    const bool fresh = g_anchorAt && GetTickCount64() - g_anchorAt < 300;
    if (fresh) memcpy(out, g_anchorHands, sizeof g_anchorHands);
    ReleaseSRWLockShared(&g_bowAimLock);
    return fresh;
}

std::string BowAimWho() {
    AcquireSRWLockShared(&g_bowAimLock);
    std::string who = g_aimStat.who;
    ReleaseSRWLockShared(&g_bowAimLock);
    return who;
}

bool BowStave(float grip[3], float tip[3], float back[3]) {
    AcquireSRWLockShared(&g_bowStaveLock);
    const bool fresh = g_bowStaveStamp && GetTickCount64() - g_bowStaveStamp < 300;
    if (fresh) {
        memcpy(grip, g_bowStaveAt, 3 * sizeof(float));
        memcpy(tip, g_bowStaveAt + 3, 3 * sizeof(float));
        memcpy(back, g_bowStaveAt + 6, 3 * sizeof(float));
    }
    ReleaseSRWLockShared(&g_bowStaveLock);
    return fresh;
}

bool BowArrow(float nock[3], float head[3], bool* drawn) {
    AcquireSRWLockShared(&g_bowArrowLock);
    const bool fresh = g_bowArrowStamp && GetTickCount64() - g_bowArrowStamp < 300;
    if (fresh) {
        memcpy(nock, g_bowArrowAt, 3 * sizeof(float));
        memcpy(head, g_bowArrowAt + 3, 3 * sizeof(float));
        if (drawn) *drawn = g_bowArrowDrawn;
    }
    ReleaseSRWLockShared(&g_bowArrowLock);
    return fresh;
}

std::string BowArrowWho() {
    AcquireSRWLockShared(&g_bowArrowLock);
    std::string s = g_bowArrowWho;
    ReleaseSRWLockShared(&g_bowArrowLock);
    return s;
}

bool BowString(float out[3]) {
    AcquireSRWLockShared(&g_bowStringLock);
    const bool fresh = g_bowStringStamp && GetTickCount64() - g_bowStringStamp < 300;
    if (fresh) memcpy(out, g_bowStringAt, sizeof g_bowStringAt);
    ReleaseSRWLockShared(&g_bowStringLock);
    return fresh;
}
#endif
int HeroClass() { return g_heroClass.load(); }
void AttachSizes(float out[2]) { out[0] = g_attachSize[0].load(); out[1] = g_attachSize[1].load(); }
#if D2RVR_FIRST_PERSON
std::string WeaponItemDiag() {
    std::string name;
    AcquireSRWLockShared(&g_itemNameLock); name = g_itemName; ReleaseSRWLockShared(&g_itemNameLock);
    char b[400];
    snprintf(b, sizeof b, "item skeletons met %u, held-weapon calls %u, remembered %u, held %u, calm %d | last item '%s' (%d bones)",
             g_itemNew.exchange(0), g_itemCalls.exchange(0), g_itemCaught.exchange(0), g_itemHeld.exchange(0), g_weaponCalm.load() ? 1 : 0,
             name.c_str(), g_itemBones.load());
    return b;
}
#endif
void WeaponDiag(float out[14]) { for (int i = 0; i < 14; ++i) out[i] = g_diag[i].load(); }
void PoseYaws(float out[3]) { for (int i = 0; i < 3; ++i) out[i] = g_poseYaw[i].load(); }

void Set(const Input& in) {
    AcquireSRWLockExclusive(&g_inLock);
    g_in = in;
    ReleaseSRWLockExclusive(&g_inLock);
    g_dollRebuild.store(in.dollRebuild);
#if D2RVR_FIRST_PERSON
    g_bowAimOn.store((in.bowAim || in.bowAxis || in.bowRest) && in.mode == 2);
    if (!in.bowAim || !in.bowRest) {   // switched off: it starts from nothing next time
        AcquireSRWLockExclusive(&g_bowAimLock);
        if (!in.bowAim) g_aimBlend.Reset();
        if (!in.bowRest) { g_restBlend.Reset(); g_restAt = 0; g_restLiftSmooth.Reset(); }
        ReleaseSRWLockExclusive(&g_bowAimLock);
    }
#endif
#if D2RVR_FIRST_PERSON
    g_bowRigOn.store(in.bowRig && in.mode == 2);
    const bool barrel = (in.xbowModel || in.xbowBarrelDebug || in.xbowAimBarrel) && in.gunStock && in.mode == 2;
    if (g_xbowBarrelOn.exchange(barrel) && !barrel) g_xbowBarrelWhy.store("not asked for");
    g_xbowBoltDiag.store(in.xbowBarrelDebug && in.gunStock && in.mode == 2);
    g_bowStringOn.store(in.bowString && in.mode == 2);
    g_bowStaveOn.store(in.bowStave && in.mode == 2);
    g_bowArrowOn.store(in.bowArrow && in.mode == 2);
    g_flailOn.store(in.flailChain && in.mode == 2);
    g_flailDamping.store(std::clamp(in.flailDamping, 0.0f, 20.0f));
    g_flailSwing.store(std::clamp(in.flailSwing, 0.0f, 2.0f));
    g_flailHit.store(std::clamp(in.flailHit, 0.0f, 2.0f));
    g_flailWeight.store(std::clamp(in.flailWeight, 0.25f, 4.0f));
    g_headOn.store(in.headsHang && in.mode == 2);
#endif
}

using ComputeFn = void (*)(uintptr_t self, float a, float b, bool c);
ComputeFn g_orig = nullptr;
std::atomic<MonsterPoseFn> g_monsterPose{nullptr};   // [weight] monster_ragdoll_pose: meleetouch::MonsterPose (vrcam)
std::atomic<MonsterPoseByWorldFn> g_monsterPoseByWorld{nullptr};   // ... and by the world pose the skinning is handed (meleetouch::MonsterPosesForSkin)
// ... the game's own pose of each monster the hook turned, as the game made it this frame: the bodies are read from it (MonsterRigs), not from the turned
// one - else the doll would push a limb its own turn has already moved, and again
struct GamePose { ULONGLONG tick = 0; std::vector<Mat> m; };
SRWLOCK g_gamePoseLock = SRWLOCK_INIT;
std::map<uintptr_t, GamePose> g_gamePoses;
bool GamePoseOf(uintptr_t self, int count, std::vector<Mat>* out) {
    bool ok = false;
    AcquireSRWLockShared(&g_gamePoseLock);
    const auto it = g_gamePoses.find(self);
    if (it != g_gamePoses.end() && (int)it->second.m.size() == count && GetTickCount64() - it->second.tick < 100) { *out = it->second.m; ok = true; }
    ReleaseSRWLockShared(&g_gamePoseLock);
    return ok;
}

// [weight] monster_ragdoll_pose: a monster whose doll is swaying gives way as the game draws it - its limbs and its lean turned in its world pose after the
// game made it (vr/doll_pose.h), as the hero's arms are. Nothing for any skeleton the doll has nothing for (one look in a table of a few).
void PoseMonsterDoll(uintptr_t self) {
    const MonsterPoseFn src = g_monsterPose.load();
    if (!src || headshadow::DollSkinningIn()) return;   // (the skinning gets the turned copy: the game's own pose stays as it made it)
    static thread_local dollpose::Pose pose;
    if (!src(self, &pose)) return;
    uintptr_t world = 0, mats = 0;
    int32_t count = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &count) || count != pose.bones || count <= 0 || count > dollpose::kMaxBones || !Get(world + 4, &mats) || !mats) return;
    static thread_local float m[dollpose::kMaxBones][16];
    if (!SafeCopy(m, (const void*)mats, sizeof(Mat) * (size_t)count)) return;
    {
        AcquireSRWLockExclusive(&g_gamePoseLock);
        if (g_gamePoses.size() > 32) g_gamePoses.clear();
        GamePose& g = g_gamePoses[self];
        g.tick = GetTickCount64();
        g.m.assign(reinterpret_cast<const Mat*>(m), reinterpret_cast<const Mat*>(m) + count);
        ReleaseSRWLockExclusive(&g_gamePoseLock);
    }
    if (dollpose::Apply(pose, m, count)) SafeWrite((void*)mats, m, sizeof(Mat) * (size_t)count);
}

// [weight] monster_ragdoll_pose, the skinning's side (headshadow::SetDollSkin): a world pose the game skins - is it a struck monster's (this frame's or the
// last's, or one like it, meleetouch::MonsterPosesForSkin)? Then its copy's matrices turned. On the render workers: one look in a table of a few.
// 0.188.7 (Serg: "нпс не шевелятся"): matched by the instance's own pose pointer for the first seconds only - the game skins from copies of it. So: by the
// pointer when it is the instance's own, else by the skeleton (the model) and the pose's own matrices against each such monster's pose now (NearestPose).
std::atomic<unsigned> g_dollByPointer{0}, g_dollByMatch{0}, g_dollUnmatched{0};
bool DollSkin(uintptr_t skeleton, uintptr_t pose, float (*mats)[16], int n) {
    const MonsterPoseByWorldFn src = g_monsterPoseByWorld.load(std::memory_order_relaxed);
    if (!src) return false;
    static thread_local dollpose::Pose cands[8];
    bool exact = false;
    const int k = src(skeleton, pose, cands, 8, &exact);
    if (k <= 0) return false;
    if (!mats) return true;   // (the composite's look first)
    int pick = exact ? 0 : -1;
    if (!exact) {
        static thread_local float now[16][dollpose::kMatchBones][16];   // each candidate's this frame's and last frame's pose (their first bones)
        const float (*ptrs[16])[16] = {};
        const int m = std::min(n, dollpose::kMatchBones);
        for (int c = 0; c < k && c < 8; ++c) {
            if (cands[c].bones != n) continue;
            for (int w = 0; w < 2; ++w) {
                uintptr_t wp = 0, mp = 0;
                if (Get(cands[c].self + (w ? 0x28 : 0x20), &wp) && Get(wp + 4, &mp) && mp && SafeCopy(now[c * 2 + w], (const void*)mp, sizeof(float) * 16 * (size_t)m))
                    ptrs[c * 2 + w] = now[c * 2 + w];
            }
        }
        const int best = dollpose::NearestPose(mats, n, ptrs, std::min(k, 8) * 2);
        pick = best >= 0 ? best / 2 : -1;
    }
    if (pick < 0) { g_dollUnmatched.fetch_add(1, std::memory_order_relaxed); return false; }
    (exact ? g_dollByPointer : g_dollByMatch).fetch_add(1, std::memory_order_relaxed);
    const bool done = cands[pick].bones == n && dollpose::Apply(cands[pick], mats, n);
    static std::atomic<ULONGLONG> toldAt{0};
    const ULONGLONG t = GetTickCount64();
    ULONGLONG was = toldAt.load();
    if (t - was >= 5000 && toldAt.compare_exchange_strong(was, t))
        NoteDoll("monster ragdoll pose - the last 5 s, skinnings of struck monsters' models: %u by their own pose, %u by matching its matrices, %u of another monster of "
                 "the model (left as the game has it)", g_dollByPointer.exchange(0), g_dollByMatch.exchange(0), g_dollUnmatched.exchange(0));
    return done;
}

void __cdecl Hook(uintptr_t self, float a, float b, bool c) {
#if D2RVR_FIRST_PERSON
    CheckLegWrite(self);   // [multiplayer] pose_share_legs: was our last write of this rig's feet still there? (nothing unless its legs were stepped lately)
#endif
    g_orig(self, a, b, c);
    g_calls++;
    frametrace::Scope trace(frametrace::kSkeleton);   // [debug] frame_trace: everything below is ours (the game's pose is done)
#if D2RVR_FIRST_PERSON
    // Off: holding the weapon's own skeleton changed nothing (09:50 log: held every frame, the axe still
    // turned) - the turn came from the body's two_hand_weapon_attach. Kept for the next weapon that needs it.
    // Off: the weapon's own skeleton never moved (10:10 log: still every frame, walking too); the turn and
    // squash came from the game laying a two-handed weapon from the right attach bone to the left one
    // (bothAttach, carry_axis). Kept for a weapon that turns out to animate its own bones.
    constexpr bool kHoldWeaponSkeleton = false;
    if (kHoldWeaponSkeleton) HoldWeaponItem(self);
#endif
    Arms(self);
    PoseMonsterDoll(self);   // [weight] monster_ragdoll_pose: a struck monster gives way as drawn
#if D2RVR_FIRST_PERSON
    PoseBowRig(self);   // [bow] rig_pose: the hero's bow, after his pose has told it where the hands are
    PoseFlail(self);    // [flail] chain: the hero's flail's chain swings free
    PoseHead(self);     // [shield] heads_hang: the hero's Necromancer head hangs from its cord
    NoteCrossbowBarrel(self);   // [hands] xbow_support_model: the hero's crossbow's barrel, read (nothing written)
#endif
    if (GetTickCount64() < g_dumpUntil.load()) {
        AcquireSRWLockExclusive(&g_lock);
        DumpOne(self);
        ReleaseSRWLockExclusive(&g_lock);
    }
}

// Starts a one-second capture into outPath. Returns false if one is running.
bool Dump(const wchar_t* outPath) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_out) fclose(g_out);
    g_out = nullptr;
    g_out = _wfsopen(outPath, L"a", _SH_DENYWR);   // appended (several presses compare), readable while the game runs
    g_seen.clear();
    if (g_out) fprintf(g_out, "skeleton dump - ComputeSelfWorldPose calls so far: %llu\n", (unsigned long long)g_calls.load());
    ReleaseSRWLockExclusive(&g_lock);
    g_dumpUntil.store(GetTickCount64() + 1000);
    return g_out != nullptr;
}

void SetGameBase(uintptr_t base) { g_gameBase.store(base); }
void SetMonsterPoseSource(MonsterPoseFn fn) { g_monsterPose.store(fn); }
void SetMonsterPoseByWorldSource(MonsterPoseByWorldFn fn) {
    g_monsterPoseByWorld.store(fn);
    headshadow::SetDollSkin(fn ? &DollSkin : nullptr);
}
bool WorldPoses(uintptr_t self, uintptr_t* cur, uintptr_t* prev, uintptr_t* skeleton) {
    *cur = *prev = 0;
    if (skeleton) {
        uintptr_t inst = 0;
        *skeleton = 0;
        if (Get(self + 0x08, &inst)) Get(inst + 0x08, skeleton);
    }
    return Get(self + 0x20, cur) && Get(self + 0x28, prev) && *cur > 0x10000;
}
void TrackHero(bool on) { g_trackHero.store(on); }
uint32_t HeroGen() { return g_heroGen.load(); }
bool HeroPosedLately(unsigned ms) { const ULONGLONG seen = g_heroSeen.load(); return seen && GetTickCount64() - seen < ms; }

#if D2RVR_FIRST_PERSON
// [diag] the held weapons' entities whose parent is the hero's: their TransformComponent local
// (+0x00) and world (+0x40) - does the game change the weapon's own offset as he walks?
std::string WeaponEntityDiag() {
    HeroXform hx;
    const char* why = "";
    if (!HeroTransform(&hx, &why)) return std::string("no hero transform: ") + why;
    std::vector<uintptr_t> selves;
    {
        AcquireSRWLockShared(&g_itemSelfLock);
        const ULONGLONG now = GetTickCount64();
        for (const auto& [s, t] : g_itemSelves) if (now - t < 1000) selves.push_back(s);
        ReleaseSRWLockShared(&g_itemSelfLock);
    }
    AcquireSRWLockExclusive(&ecs::g_lock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } unlock;
    std::string out;
    int children = 0;
    for (const uintptr_t s : selves) {
        uint32_t e = 0, i = 0; uintptr_t cb = 0;
        if (!ecs::EntityOf(s, &e) || !ecs::Dense(ecs::g_xs, e, &i) || !Get(ecs::g_xs + 0x38, &cb) || !cb) continue;
        const uintptr_t comp = cb + (uintptr_t)i * kTransformSize;
        uint32_t parent = 0xFFFFFFFFu; Get(comp + 0xC0, &parent);
        if ((parent & 0xFFFFF) != (hx.entity & 0xFFFFF)) continue;
        ++children;
        float l[16] = {}, w[16] = {};
        SafeCopy(l, (const void*)comp, 64); SafeCopy(w, (const void*)(comp + 0x40), 64);
        char b[300];
        snprintf(b, sizeof b, " [entity %u local t %.2f %.2f %.2f x %.2f %.2f %.2f y %.2f %.2f %.2f | world t %.1f %.1f %.1f]", e,
                 l[12], l[13], l[14], l[0], l[1], l[2], l[4], l[5], l[6], w[12], w[13], w[14]);
        out += b;
        // where it should be: its local x our R attach (model space) x the hero's matrix - the late
        // (+0x40, after the poses) and the early (+0x00, before them) one; against where it is
        Mat L, W, hl, hw, ra;
        memcpy(L.m, l, 64); memcpy(W.m, w, 64);
        SafeCopy(hl.m, (const void*)hx.comp, 64); SafeCopy(hw.m, (const void*)(hx.comp + 0x40), 64);
        AcquireSRWLockShared(&g_rAttachLock); ra = g_rAttach; ReleaseSRWLockShared(&g_rAttachLock);
        auto off = [&](const Mat& want, float* dist, float* deg) {
            *dist = Len(Sub(Pos(want), Pos(W)));
            const V3 a = Norm(V3{want.m[4], want.m[5], want.m[6]}), bb = Norm(V3{W.m[4], W.m[5], W.m[6]});
            *deg = acosf(std::clamp(Dot(a, bb), -1.0f, 1.0f)) * 57.2958f;
        };
        float d1, a1, d2, a2;
        off(Mul(Mul(L, ra), hw), &d1, &a1);
        off(Mul(Mul(L, ra), hl), &d2, &a2);
        snprintf(b, sizeof b, " | from our R attach x hero late: off %.2f units %.0f deg, x hero early: off %.2f units %.0f deg", d1, a1, d2, a2);
        out += b;
        {   // the third matrix at +0x80: what is it against W and against where we want it?
            Mat M2; SafeCopy(M2.m, (const void*)(comp + 0x80), 64);
            const Mat want = Mul(Mul(L, ra), hw);
            const V3 a = Norm(V3{M2.m[4], M2.m[5], M2.m[6]}), bb = Norm(V3{want.m[4], want.m[5], want.m[6]}), c = Norm(V3{W.m[4], W.m[5], W.m[6]});
            snprintf(b, sizeof b, " | +0x80 t %.1f %.1f %.1f (W t %.1f %.1f %.1f) y-axis vs want %.0f deg vs W %.0f deg, row3w %.2f", M2.m[12], M2.m[13], M2.m[14],
                     W.m[12], W.m[13], W.m[14], acosf(std::clamp(Dot(a, bb), -1.0f, 1.0f)) * 57.2958f, acosf(std::clamp(Dot(a, c), -1.0f, 1.0f)) * 57.2958f, M2.m[15]);
            out += b;
        }
        {   // the bone the game used, X = L^-1 W H^-1 (model space), against ours: X = F x R, F in R's own frame
            const Mat X = Mul(Mul(InvAffine(L), W), InvAffine(hw));
            const Mat F = Mul(X, InvAffine(ra));
            const float tr = F.m[0] + F.m[5] + F.m[10];
            const float ang = acosf(std::clamp((tr - 1.0f) * 0.5f, -1.0f, 1.0f)) * 57.2958f;
            V3 ax = {F.m[6] - F.m[9], F.m[8] - F.m[2], F.m[1] - F.m[4]};   // row-vector rotation: its axis (up to sign)
            ax = Len(ax) > 1e-5f ? Norm(ax) : V3{0, 0, 0};
            snprintf(b, sizeof b, " | F in R's frame: t %.3f %.3f %.3f turn %.1f deg about %.2f %.2f %.2f diag %.2f %.2f %.2f | row lengths X %.2f %.2f %.2f ours %.2f %.2f %.2f",
                     F.m[12], F.m[13], F.m[14], ang, ax.x, ax.y, ax.z, F.m[0], F.m[5], F.m[10],
                     Len(V3{X.m[0], X.m[1], X.m[2]}), Len(V3{X.m[4], X.m[5], X.m[6]}), Len(V3{X.m[8], X.m[9], X.m[10]}),
                     Len(V3{ra.m[0], ra.m[1], ra.m[2]}), Len(V3{ra.m[4], ra.m[5], ra.m[6]}), Len(V3{ra.m[8], ra.m[9], ra.m[10]}));
            out += b;
            std::vector<Mat> po; int at;
            AcquireSRWLockShared(&g_rAttachLock); po = g_diagOld; at = g_diagAttach; ReleaseSRWLockShared(&g_rAttachLock);
            if (at >= 0 && at < (int)po.size()) {
                const Mat& g = po[at];
                snprintf(b, sizeof b, " | game R attach rows %.2f %.2f %.2f", Len(V3{g.m[0], g.m[1], g.m[2]}), Len(V3{g.m[4], g.m[5], g.m[6]}), Len(V3{g.m[8], g.m[9], g.m[10]}));
                out += b;
            }
        }
        if (d1 > 0.05f || a1 > 3.0f) {   // which bone, in which pose, does it follow instead? the best three
            std::vector<Mat> po, pn; int wr, at; uintptr_t sk;
            AcquireSRWLockShared(&g_rAttachLock); po = g_diagOld; pn = g_diagNew; wr = g_diagWrist; at = g_diagAttach; sk = g_diagSkel; ReleaseSRWLockShared(&g_rAttachLock);
            std::vector<std::string> names;
            AcquireSRWLockShared(&g_namesLock);
            if (const auto it = g_boneNames.find(sk); it != g_boneNames.end()) names = it->second;
            ReleaseSRWLockShared(&g_namesLock);
            struct Hit { float score, dist, deg; int bone; const char* pose; };
            std::vector<Hit> hits;
            auto test = [&](const Mat& bone, int i, const char* pose) {
                float d, a; off(Mul(Mul(L, bone), hw), &d, &a);
                hits.push_back({d + a * 0.01f, d, a, i, pose});
            };
            for (int i = 0; i < (int)po.size() && i < (int)pn.size(); ++i) { test(po[i], i, "game"); test(pn[i], i, "ours"); }
            if (wr >= 0 && at >= 0 && at < (int)po.size())   // the game's attach-in-wrist on our wrist
                test(Mul(Mul(po[at], InvAffine(po[wr])), pn[wr]), at, "game-local-on-our-wrist");
            std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.score < y.score; });
            for (size_t k = 0; k < hits.size() && k < 3; ++k) {
                snprintf(b, sizeof b, " | best %zu: %s %s (%d) off %.2f %.0f deg", k + 1, hits[k].pose,
                         hits[k].bone < (int)names.size() ? names[hits[k].bone].c_str() : "?", hits[k].bone, hits[k].dist, hits[k].deg);
                out += b;
            }
        }
    }
    char head[120];
    snprintf(head, sizeof head, "hero entity %u, %d held-weapon skeletons lately, %d of them his children:", hx.entity, (int)selves.size(), children);
    return head + out;
}
#endif

bool EntityTransform(uint32_t entity, uintptr_t* comp) {
    AcquireSRWLockExclusive(&ecs::g_lock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } unlock;
    if (!ecs::IsStorage(ecs::g_xs, ecs::g_vtX) || !ecs::IsStorage(ecs::g_ss, ecs::g_vtS)) {
        const ULONGLONG now = GetTickCount64();
        if (now - ecs::g_lastStorageLook < 500) return false;
        ecs::g_lastStorageLook = now;
        if (!ecs::FindStorages()) return false;
        ecs::g_cachedSelf = 0;
    }
    uint32_t i = 0; uintptr_t cb = 0;
    if (!ecs::Dense(ecs::g_xs, entity, &i) || !Get(ecs::g_xs + 0x38, &cb) || !cb) return false;
    *comp = cb + (uintptr_t)i * kTransformSize;
    return true;
}

// ---- [weight] monster_bodies: the monsters' own skeletons (vr/monster_rig.h) ----
// Off the pose hook: the renderer's ECS lists every SkeletonInstance with its entity, and the entity's TransformComponent is where the model stands. A
// monster's bones are its world pose (model space, as the game posed it this frame) times that transform; their thickness is its meshes' boxes, read once
// per model. Read-only, every read guarded; the ECS lock is held only to learn the storages, not for the walk.
namespace {
SRWLOCK g_rigLock = SRWLOCK_INIT;
struct RigRead {
    monsterrig::Boxes boxes;
    ULONGLONG retryAt;   // 0: for good; else read again from then (no model found yet: the unit's parts may not be attached yet)
    monsterhumanoid::Verdict human;   // [weight] monster_ragdoll: its skeleton's bone names (vr/monster_humanoid.h)
    bool upright = false;             // ... and seen standing up (Upright, kUprightRun reads running; latched)
    int uprightRun = 0;               // ... reads running it has been
    const char* pose = "";            // ... what the last read said of its pose
};
std::map<std::pair<uintptr_t, uintptr_t>, RigRead> g_rigBoxes;   // (the parts' resources, skeleton) -> its bones' boxes (n == 0: none, why says)
// The storages a unit's body is read from: its parts' ModelComponents, the root's AttachedChildrenComponent, the parts' UnitPartComponents (vr/monster_rig.h).
enum BodyPool { kPoolModel, kPoolChildren, kPoolPart, kPools };
struct Pool {
    uint64_t seq;
    uintptr_t st, vt;   // the storage and its vtable then (under ecs::g_lock)
};
Pool g_pools[kPools] = {{RVA_TYPE_SEQ_MODEL, 0, 0}, {RVA_TYPE_SEQ_CHILDREN, 0, 0}, {RVA_TYPE_SEQ_PART, 0, 0}};
ULONGLONG g_poolLook = 0;
std::atomic<bool> g_rigDumpWanted{false};   // [debug] monster_rig_dump: one model's reads, written out once
std::atomic<bool> g_heldWanted{false};      // [weight] monster_shields: the held parts read too (MonsterRig::held)
std::atomic<bool> g_ragdollAll{false};      // [weight] monster_ragdoll_all: every monster a doll, bodies from bone segments where no meshes
std::mutex g_stillLock;                     // [weight] monster_ragdoll: the corpse watch (vr/corpse_watch.h), by entity
std::unordered_map<uint32_t, corpsewatch::Watch> g_still;
std::string g_rigDump;                      // (under g_rigLock) what is to be logged
std::set<std::string> g_rigPathsTold;       // (under g_rigLock) the paths told in the dump's short lines

// The body's storages, as FindStorages takes the other two (under ecs::g_lock; those not found looked for at most twice a second); 0: not found.
void BodyStorages(uintptr_t out[kPools]) {
    bool all = true;
    for (const Pool& p : g_pools) all = all && ecs::IsStorage(p.st, p.vt);
    const ULONGLONG now = GetTickCount64();
    if (!all && now - g_poolLook >= 500) {
        g_poolLook = now;
        const uintptr_t reg = d2rsig::Addr(RVA_ECS_REGISTRY);
        uintptr_t b = 0, e = 0;
        if (reg && Get(reg, &b) && Get(reg + 8, &e) && e > b && (e - b) % 0x28 == 0)
            for (Pool& p : g_pools) {
                if (ecs::IsStorage(p.st, p.vt)) continue;
                p.st = p.vt = 0;
                const uintptr_t seq = d2rsig::Addr(p.seq);
                uintptr_t st = 0, vt = 0;
                if (!seq) continue;
                const uint32_t ix = (uint32_t)((int (*)())seq)();
                if (ix >= (e - b) / 0x28 || !Get(b + ix * 0x28 + 0x20, &st) || !st || !Get(st, &vt) || !vt) continue;
                p.st = st;
                p.vt = vt;
            }
    }
    for (int i = 0; i < kPools; ++i) out[i] = ecs::IsStorage(g_pools[i].st, g_pools[i].vt) ? g_pools[i].st : 0;
}

// [debug] monster_rig_dump: what one model's reads give, for the next headset test to prove the layout (once; the paths of every model in short lines).
void DumpRig(const std::string& path, uint32_t entity, uintptr_t self, uintptr_t inst, uintptr_t skelp, uintptr_t modelInst, const monsterrig::Boxes& granny,
             const monsterrig::Boxes& meshes, int nMeshes, bool ownModel, int nModels, const monsterrig::Parts& parts) {
    const auto read = [](uintptr_t a, void* d, size_t bytes) { return a > 0x10000 && SafeCopy(d, (const void*)a, bytes); };
    char b[2048];
    int w = snprintf(b, sizeof b, "monster rig dump - '%s' (entity %X, SkeletonInstance %llX, granny instance %llX, skeleton %llX, first model instance %llX): "
                     "the .skeleton's Granny model: %s (%d boxes); the root's own ModelComponent: %s; its children: %d, %d with a model, %d held (weapon/shield), "
                     "%d models read, slots",
                     path.c_str(), entity, (unsigned long long)self, (unsigned long long)inst, (unsigned long long)skelp, (unsigned long long)modelInst,
                     granny.n ? "boxes" : granny.why, granny.n, ownModel ? "yes" : "none", parts.children, parts.models, parts.held, nModels);
    for (int i = 0; i < parts.models && i < monsterrig::kMaxModels && w < 1000; ++i) w += snprintf(b + w, sizeof b - w, " %d", parts.slots[i]);
    w += snprintf(b + w, sizeof b - w, "; the game's meshes: %s (%d meshes, %d with boxes, %d boxes)", meshes.n ? "boxes" : meshes.why, nMeshes, meshes.meshes, meshes.n);
    uintptr_t res = 0, data = 0, bones = 0;
    uint8_t state = 0;
    uint64_t count = 0;
    int32_t boneCount = 0;
    if (read(modelInst + monsterrig::kInstanceResource, &res, 8) && read(res + monsterrig::kResourceState, &state, 1)) w += snprintf(b + w, sizeof b - w, "; resource %llX state %u", (unsigned long long)res, state);
    if (read(skelp + monsterrig::kSkelCount, &boneCount, 4) && read(skelp + monsterrig::kSkelBones, &bones, 8)) {
        w += snprintf(b + w, sizeof b - w, "; skeleton %d bones:", boneCount);
        for (int i = 0; i < boneCount && i < 6 && w < 1500; ++i) {
            uintptr_t np = 0;
            char n[48] = "";
            if (read(bones + (uintptr_t)i * monsterrig::kBoneStride, &np, 8)) grannymesh::String(read, np, n);
            w += snprintf(b + w, sizeof b - w, " '%s'", n);
        }
    }
    if (read(modelInst + monsterrig::kInstanceMeshes, &data, 8) && read(modelInst + monsterrig::kInstanceMeshes + 8, &count, 8)) {
        w += snprintf(b + w, sizeof b - w, "; meshes at %llX x %llu:", (unsigned long long)data, (unsigned long long)count);
        for (uint64_t m = 0; m < count && m < 4 && w < 1800; ++m) {
            uintptr_t h = 0, mesh = 0, binds = 0;
            int32_t nb = 0;
            read(data + m * 8, &h, 8);
            read(h, &mesh, 8);
            read(mesh + monsterrig::kMeshBoneCount, &nb, 4);
            read(mesh + monsterrig::kMeshBones, &binds, 8);
            w += snprintf(b + w, sizeof b - w, " [%llu] handle %llX mesh %llX, %d bindings at %llX", (unsigned long long)m, (unsigned long long)h, (unsigned long long)mesh, nb,
                          (unsigned long long)binds);
            for (int k = 0; k < nb && k < 2 && w < 1900; ++k) {
                uintptr_t np = 0;
                char n[48] = "";
                float mn[3] = {}, mx[3] = {};
                read(binds + (uintptr_t)k * monsterrig::kBindingStride, &np, 8);
                grannymesh::String(read, np, n);
                read(binds + (uintptr_t)k * monsterrig::kBindingStride + monsterrig::kBindingMin, mn, 12);
                read(binds + (uintptr_t)k * monsterrig::kBindingStride + monsterrig::kBindingMax, mx, 12);
                w += snprintf(b + w, sizeof b - w, " {'%s' %.2f %.2f %.2f .. %.2f %.2f %.2f}", n, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
            }
        }
    }
    AcquireSRWLockExclusive(&g_rigLock);
    g_rigDump = b;
    ReleaseSRWLockExclusive(&g_rigLock);
}
}   // namespace

void SetRigDump(bool on) { g_rigDumpWanted.store(on); }
void SetHeldWanted(bool on) { g_heldWanted.store(on); }
void SetRagdollAll(bool on) { g_ragdollAll.store(on); }
std::string RigDumpTakeIn() {
    std::string s;
    AcquireSRWLockExclusive(&g_rigLock);
    s.swap(g_rigDump);
    ReleaseSRWLockExclusive(&g_rigLock);
    return s;
}

uintptr_t EntityModelInstance(uint32_t entity) {
    uintptr_t pools[kPools] = {};
    {
        AcquireSRWLockExclusive(&ecs::g_lock);
        struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } unlock;
        BodyStorages(pools);
    }
    const uintptr_t ms = pools[kPoolModel];
    uintptr_t mcb = 0, inst = 0;
    uint32_t i = 0;
    return ms && Get(ms + 0x38, &mcb) && mcb && ecs::Dense(ms, entity, &i) && Get(mcb + (uintptr_t)i * kModelCompSize + monsterrig::kModelCompInstance, &inst) ? inst : 0;
}
int HeroPartModels(uint32_t entity, uintptr_t* models, int cap, uint64_t* key) {
    if (key) *key = 0;
    if (!entity || !models || cap <= 0) return 0;
    uintptr_t pools[kPools] = {};
    {
        AcquireSRWLockExclusive(&ecs::g_lock);
        struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } unlock;
        BodyStorages(pools);
    }
    const uintptr_t ms = pools[kPoolModel], cs = pools[kPoolChildren], ps = pools[kPoolPart];
    uintptr_t mcb = 0, ccb = 0, pcb = 0;
    if (ms && !Get(ms + 0x38, &mcb)) mcb = 0;
    if (cs && !Get(cs + 0x38, &ccb)) ccb = 0;
    if (ps && !Get(ps + 0x38, &pcb)) pcb = 0;
    const auto read = [](uintptr_t a, void* d, size_t bytes) { return a > 0x10000 && SafeCopy(d, (const void*)a, bytes); };
    const auto modelOf = [&](uint32_t e) -> uintptr_t {   // (as MonsterRigs)
        uint32_t i = 0;
        uintptr_t inst = 0;
        return mcb && ecs::Dense(ms, e, &i) && Get(mcb + (uintptr_t)i * kModelCompSize + monsterrig::kModelCompInstance, &inst) ? inst : 0;
    };
    const auto slotOf = [&](uint32_t e) -> int {
        uint32_t i = 0;
        uint8_t slot = 0;
        return pcb && ecs::Dense(ps, e, &i) && Get(pcb + (uintptr_t)i * monsterrig::kPartCompSize + monsterrig::kPartSlot, &slot) ? (int)slot : -1;
    };
    int n = 0;
    const uintptr_t own = modelOf(entity);
    if (own >= 0x10000) models[n++] = own;
    if (ccb && n < cap) {
        uint32_t ci = 0;
        const uintptr_t children = ecs::Dense(cs, entity, &ci) ? ccb + (uintptr_t)ci * monsterrig::kChildrenCompSize : 0;
        n += monsterrig::PartModels(read, children, modelOf, slotOf, models + n, cap - n);
    }
    uint64_t h = 0;
    for (int i = 0; i < n; ++i) {
        uintptr_t res = 0;
        if (Get(models[i] + monsterrig::kInstanceResource, &res)) h = h * 0x9E3779B97F4A7C15ull + res;
    }
    if (key) *key = h;
    return n;
}

int HeroBodyCaps(monsterrig::Capsule* out, int cap, const char** why) {
    static const char* const kNoHero = "the hero is not read";
    if (why) *why = "";
    if (!out || cap <= 0) return 0;
    HeroXform hx;
    if (!HeroTransform(&hx, why) || !hx.self || !hx.comp) { if (why && !**why) *why = kNoHero; return 0; }
    uintptr_t inst = 0, skelp = 0, world = 0, mats = 0;
    int32_t count = 0;
    Mat xform;
    if (!Get(hx.self + 0x08, &inst) || !Get(inst + 0x08, &skelp) || !Get(hx.self + 0x20, &world) || !Get(world, &count) || count <= 0 ||
        count > monsterrig::kMaxBones || !Get(world + 4, &mats) || !mats || !SafeCopy(xform.m, (const void*)hx.comp, sizeof xform.m)) {
        if (why) *why = "his pose does not read";
        return 0;
    }
    // his meshes' boxes and his skeleton's verdict, kept by his parts (another armour: read again)
    uintptr_t models[monsterrig::kMaxModels] = {};
    uint64_t key = 0;
    const int nModels = HeroPartModels(hx.entity, models, monsterrig::kMaxModels, &key);
    struct Kept { uint64_t key = 0; uintptr_t skel = 0; monsterrig::Boxes boxes; monsterhumanoid::Verdict human; uint8_t keep[monsterhumanoid::kMaxBones] = {}; bool ok = false; };
    static Kept kept;   // (the touch's timer thread only)
    if (!kept.ok || kept.key != key || kept.skel != skelp) {
        const auto read = [](uintptr_t a, void* d, size_t bytes) { return a > 0x10000 && SafeCopy(d, (const void*)a, bytes); };
        kept = Kept{};
        kept.key = key;
        kept.skel = skelp;
        int nMeshes = 0;
        if (nModels > 0) monsterrig::BoxesOfModels(read, models, nModels, skelp, &kept.boxes, &nMeshes);
        kept.human = monsterhumanoid::JudgeSkeleton(read, skelp);
        kept.ok = kept.boxes.n > 0 && kept.human.bones == count && monsterhumanoid::OwnBodyBones(kept.human, kept.keep);
        if (!kept.ok) {
            if (why) *why = !kept.boxes.n ? "his meshes' boxes do not read" : kept.human.bones != count ? "his skeleton does not read" : "his arms or head are not found by name";
            kept.key = key;   // (asked again only when his parts change)
            return 0;
        }
    }
    if (!kept.ok) { if (why) *why = "his arms or head are not found by name"; return 0; }
    monsterrig::Capsule caps[monsterrig::kMaxBoxes];
    int nc = 0;
    for (int b = 0; b < kept.boxes.n && nc < (int)std::size(caps); ++b) {
        const int bone = kept.boxes.b[b].bone;
        if (bone < 0 || bone >= count || !kept.keep[bone] || !monsterrig::AtItsBone(kept.boxes.b[b])) continue;
        Mat bm;
        if (!SafeCopy(bm.m, (const void*)(mats + (uintptr_t)bone * 64), sizeof bm.m)) continue;
        const Mat w = Mul(bm, xform);
        if (monsterrig::CapsuleOf(w.m, kept.boxes.b[b], &caps[nc])) ++nc;
    }
    const int k = std::min(monsterrig::Keep(caps, nc), cap);
    for (int i = 0; i < k; ++i) out[i] = caps[i];
    if (!k && why) *why = "no box of his trunk or legs made a capsule";
    return k;
}

size_t MonsterRigs(float x, float z, float reach, MonsterRig* out, size_t cap) {
    if (!out || !cap || !std::isfinite(x + z + reach)) return 0;
    uintptr_t xs = 0, ss = 0, pools[kPools] = {};
    {
        AcquireSRWLockExclusive(&ecs::g_lock);
        struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } unlock;
        if (!ecs::IsStorage(ecs::g_xs, ecs::g_vtX) || !ecs::IsStorage(ecs::g_ss, ecs::g_vtS)) {
            const ULONGLONG now = GetTickCount64();
            if (now - ecs::g_lastStorageLook < 500) return 0;
            ecs::g_lastStorageLook = now;
            if (!ecs::FindStorages()) return 0;
            ecs::g_cachedSelf = 0;
        }
        xs = ecs::g_xs;
        ss = ecs::g_ss;
        BodyStorages(pools);   // (the ModelComponents' not found: the Granny model's meshes are all there is)
    }
    const uintptr_t ms = pools[kPoolModel], cs = pools[kPoolChildren], ps = pools[kPoolPart];
    uintptr_t mcb = 0, ccb = 0, pcb = 0;
    if (ms && !Get(ms + 0x38, &mcb)) mcb = 0;
    if (cs && !Get(cs + 0x38, &ccb)) ccb = 0;
    if (ps && !Get(ps + 0x38, &pcb)) pcb = 0;
    // an entity's model instance (its ModelComponent's first qword; 0: none) and its part's slot (its UnitPartComponent's first byte; -1: none)
    const auto modelOf = [&](uint32_t e) -> uintptr_t {
        uint32_t i = 0;
        uintptr_t inst = 0;
        return mcb && ecs::Dense(ms, e, &i) && Get(mcb + (uintptr_t)i * kModelCompSize + monsterrig::kModelCompInstance, &inst) ? inst : 0;
    };
    const auto slotOf = [&](uint32_t e) -> int {
        uint32_t i = 0;
        uint8_t slot = 0;
        return pcb && ecs::Dense(ps, e, &i) && Get(pcb + (uintptr_t)i * monsterrig::kPartCompSize + monsterrig::kPartSlot, &slot) ? (int)slot : -1;
    };
    uintptr_t pb = 0, pe = 0, cb = 0, xcb = 0;
    if (!Get(ss + 0x20, &pb) || !Get(ss + 0x28, &pe) || pe <= pb || !Get(ss + 0x38, &cb) || !cb || !Get(xs + 0x38, &xcb) || !xcb) return 0;
    const size_t n = (pe - pb) / 4;
    if (n > 20000) return 0;
    std::vector<uint64_t> comps(n * (kSkeletonCompSize / 8));
    std::vector<uint32_t> packed(n);
    if (!SafeCopy(comps.data(), (const void*)cb, n * kSkeletonCompSize) || !SafeCopy(packed.data(), (const void*)pb, n * 4)) return 0;
    const uintptr_t hero = g_heroSelf.load();
    struct Model { uintptr_t self, comp; uint32_t entity; float d2; float t[3]; };   // (not "near": windows.h has that word)
    std::vector<Model> nearby;
    const bool heldWanted = g_heldWanted.load(std::memory_order_relaxed);
    const bool all = g_ragdollAll.load(std::memory_order_relaxed);   // [weight] monster_ragdoll_all
    for (size_t i = 0; i < n; ++i) {
        const uintptr_t self = (uintptr_t)comps[i * (kSkeletonCompSize / 8)];
        if (!self || self == hero) continue;
        uint32_t ti = 0, parent = 0;
        if (!ecs::Dense(xs, packed[i], &ti)) continue;
        const uintptr_t comp = xcb + (uintptr_t)ti * kTransformSize;
        if (!Get(comp + 0xC0, &parent) || (parent & 0xFFFFF) != 0xFFFFF) continue;   // a model hung off another (a weapon in a hand, an armour piece): not a body of its own
        Model c{self, comp, packed[i], 0.0f, {}};
        if (!SafeCopy(c.t, (const void*)(comp + 12 * 4), sizeof c.t) || !std::isfinite(c.t[0] + c.t[1] + c.t[2])) continue;
        c.d2 = (c.t[0] - x) * (c.t[0] - x) + (c.t[2] - z) * (c.t[2] - z);
        if (c.d2 <= reach * reach) nearby.push_back(c);
    }
    std::sort(nearby.begin(), nearby.end(), [](const Model& a, const Model& b) { return a.d2 < b.d2; });
    const auto read = [](uintptr_t a, void* d, size_t bytes) { return a > 0x10000 && SafeCopy(d, (const void*)a, bytes); };
    size_t k = 0;
    for (const Model& c : nearby) {
        if (k >= cap) break;
        MonsterRig& r = out[k];
        r = MonsterRig{};
        r.entity = c.entity;
        r.self = c.self;
        memcpy(r.root, c.t, sizeof r.root);
        uintptr_t nameObj = 0, nameP = 0, inst = 0, skelp = 0, model = 0;
        Get(c.self, &nameObj);
        Get(nameObj + 8, &nameP);
        const std::string path = Str(nameP);
        if (path.find("/player/") != std::string::npos) continue;   // a hero (another player's, our own corpse)
        if (!monsterrig::BodyPath(path.c_str())) {   // an item on the ground, gold, arrows, a missile: no body (the dump says which paths were left out)
            if (g_rigDumpWanted.load()) {
                AcquireSRWLockExclusive(&g_rigLock);
                if (g_rigPathsTold.size() < 64 && g_rigPathsTold.insert(path).second) g_rigDump = "monster rig dump - not a body: '" + path + "'";
                ReleaseSRWLockExclusive(&g_rigLock);
            }
            continue;
        }
        const size_t slash = path.find_last_of('/');
        snprintf(r.model, sizeof r.model, "%s", slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1);
        if (!Get(c.self + 0x08, &inst) || !Get(inst + 0x08, &skelp) || !Get(inst, &model)) { r.why = "the model instance does not read"; ++k; continue; }
        // the game's meshes of this unit: the root's own ModelComponent if it has one, and its body parts' - the children in its
        // AttachedChildrenComponent, the weapons and the shield left out (vr/monster_rig.h PartModels, BoxesOfModels)
        uintptr_t models[monsterrig::kMaxModels] = {}, modelRes = 0;
        int nModels = 0;
        monsterrig::Parts parts;
        parts.children = -1;
        const uintptr_t own = modelOf(c.entity);
        if (own >= 0x10000) models[nModels++] = own;
        if (ccb) {
            uint32_t ci = 0;
            const uintptr_t children = ecs::Dense(cs, c.entity, &ci) ? ccb + (uintptr_t)ci * monsterrig::kChildrenCompSize : 0;
            nModels += monsterrig::PartModels(read, children, modelOf, slotOf, models + nModels, monsterrig::kMaxModels - nModels, &parts);
        }
        for (int i = 0; i < nModels; ++i) {   // the parts' resources name the body (a variant of the same skeleton may wear other parts)
            uintptr_t res = 0;
            if (Get(models[i] + monsterrig::kInstanceResource, &res)) modelRes = modelRes * 0x9E3779B97F4A7C15ull + res;
        }
        monsterrig::Boxes boxes;
        monsterhumanoid::Verdict human;
        bool upright = false;
        const char* pose = "";
        const auto key = std::make_pair(modelRes ? modelRes : model, skelp);
        {
            bool have = false;
            AcquireSRWLockShared(&g_rigLock);
            const auto it = g_rigBoxes.find(key);
            if (it != g_rigBoxes.end() && (!it->second.retryAt || GetTickCount64() < it->second.retryAt)) {
                boxes = it->second.boxes;
                human = it->second.human;
                upright = it->second.upright;
                pose = it->second.pose;
                have = true;
            }
            ReleaseSRWLockShared(&g_rigLock);
            if (!have) {
                monsterrig::Boxes granny, meshes;
                int nMeshes = 0;
                monsterrig::BoxesOf(read, inst, skelp, &granny);   // (the .skeleton's own Granny model: no meshes in the game - kept for a model that has them)
                if (nModels) monsterrig::BoxesOfModels(read, models, nModels, skelp, &meshes, &nMeshes);
                else if (!ms) meshes.why = "the ModelComponent storage is not found";
                else if (!cs) meshes.why = "the root has no ModelComponent and the AttachedChildrenComponent storage is not found";
                else if (parts.children < 0) meshes.why = "the root has no ModelComponent and no children";
                else if (parts.models && parts.models == parts.held) meshes.why = "its parts with a model are all held (weapon, shield)";
                else meshes.why = "the root has no ModelComponent, nor have its children";
                boxes = granny.n ? granny : meshes;
                if (!boxes.n && !granny.n) boxes.why = meshes.why;
                if (g_rigDumpWanted.exchange(false)) DumpRig(path, c.entity, c.self, inst, skelp, models[0], granny, meshes, nMeshes, own >= 0x10000, nModels, parts);
                human = monsterhumanoid::JudgeSkeleton(read, skelp);   // [weight] monster_ragdoll: a humanoid by its bone names? (once a model)
                const bool settled = boxes.n || std::strcmp(meshes.why, "the model is not loaded yet") != 0;   // (one still loading is asked again)
                if (settled) {
                    AcquireSRWLockExclusive(&g_rigLock);
                    if (g_rigBoxes.size() > 256) g_rigBoxes.clear();
                    g_rigBoxes[key] = RigRead{boxes, boxes.n || nModels ? 0 : GetTickCount64() + 1000, human, false, 0, ""};
                    ReleaseSRWLockExclusive(&g_rigLock);
                }
            }
        }
        r.bones = boxes.bones;
        r.boxes = boxes.n;
        // [weight] monster_ragdoll_all: a skeleton with no part meshes to read boxes from (its body comes from other models, or none read) - its limbs from
        // its bones' segments as posed (below), when its skeleton read
        const bool fromBones = !boxes.n && all && human.bones > 0 && human.bones <= monsterrig::kMaxBones;
        if (!boxes.n && !fromBones) { r.why = boxes.why; ++k; continue; }
        uintptr_t world = 0, mats = 0;
        int32_t count = 0;
        Mat xform;
        if (!Get(c.self + 0x20, &world) || !Get(world, &count) || count != (fromBones ? human.bones : boxes.bones) || !Get(world + 4, &mats) || !mats ||
            !SafeCopy(xform.m, (const void*)c.comp, sizeof xform.m)) { r.why = "its pose does not read"; ++k; continue; }
        monsterrig::Capsule caps[monsterrig::kMaxBoxes];
        monsterrig::BoneBox offLimb[16];   // a humanoid's limb bones whose box is not at the bone (a robe's): their capsule along the bone (monsterrig::AlongBone)
        int nOffLimb = 0;
        std::vector<Mat> game;   // [weight] monster_ragdoll_pose: the game's own pose, where the hook turned this one's bones
        const bool turned = GamePoseOf(c.self, count, &game);
        if (fromBones) {   // the segments to each bone's children, in its frame (vr/bone_segments.h; no box for the root, "anim", a finger, a helper)
            static float posed[monsterrig::kMaxBones][16];   // (the timer's thread only)
            bool all2 = true;
            for (int b = 0; b < count && all2; ++b) {
                if (turned) std::memcpy(posed[b], game[b].m, sizeof posed[b]);
                else if (!SafeCopy(posed[b], (const void*)(mats + (uintptr_t)b * 64), sizeof posed[b])) all2 = false;
            }
            boxes.n = all2 ? bonesegments::Boxes(human.parent, count, posed, human.notLimb, boxes.b, monsterrig::kMaxBoxes) : 0;
            boxes.bones = count;
            r.bones = count;
            r.boxes = boxes.n;
            r.segments = true;
            if (!boxes.n) { r.why = "no part meshes, and its bones made no segments"; ++k; continue; }
        }
        int nc = 0;
        // a humanoid's body bones only (its limbs, the trunk, the head - not a rope or a strip of cloth off it), and only boxes that sit at their bone
        static bool body[monsterrig::kMaxBones];
        const bool judged = human.humanoid && count <= monsterrig::kMaxBones && human.bones == count;
        if (judged) monsterrig::BodyBones(human.parent, human.limbOf, count, human.headBone, body, human.bodyWord);
        {
            uintptr_t bonesP = 0;
            Get(skelp + monsterrig::kSkelBones, &bonesP);
            int w = 0;
            std::vector<std::pair<std::string, int>> stems;
            for (int b = 0; b < boxes.n; ++b) {
                const int bone = boxes.b[b].bone;
                // (every monster a doll: a bone whose name says it is no limb - "anim" on the floor under the hips, a finger, a helper - is left out too)
                const bool notLimb = all && !judged && bone >= 0 && bone < count && human.bones == count && human.notLimb[bone];
                const bool off = !monsterrig::AtItsBone(boxes.b[b]), out = (judged && bone >= 0 && bone < count && !body[bone]) || notLimb;
                if (!off && !out) continue;
                if (off && !out && judged && bone >= 0 && bone < count && human.limbOf[bone] && nOffLimb < (int)std::size(offLimb)) offLimb[nOffLimb++] = boxes.b[b];   // (along its bone, below)
                ++(off ? r.offBone : r.notBody);
                char nm[monsterrig::kNameLen] = "";
                uintptr_t np = 0;
                if (bonesP && Get(bonesP + (uintptr_t)bone * monsterrig::kBoneStride + monsterrig::kBoneName, &np)) grannymesh::String(read, np, nm);
                // (each kind once, with how many: the name's stem - its digits out - "spike_jnt x60 (not body)")
                std::string stem;
                for (const char* q = nm; *q; ++q)
                    if (!(*q >= '0' && *q <= '9') && !(*q == '_' && !stem.empty() && stem.back() == '_')) stem += *q;
                stem += off ? " (off)" : " (not body)";
                bool found = false;
                for (auto& s : stems) if (s.first == stem) { ++s.second; found = true; }
                if (!found && stems.size() < 40) stems.push_back({stem, 1});
                boxes.b[b].bone = -1 - bone;   // (left out below; the cache keeps its own copy)
            }
            for (const auto& s : stems)
                if (w < (int)sizeof r.leftOut - 60)
                    w += s.second > 1 ? snprintf(r.leftOut + w, sizeof r.leftOut - w, "%s%s x%d", w ? ", " : "", s.first.c_str(), s.second)
                                      : snprintf(r.leftOut + w, sizeof r.leftOut - w, "%s%s", w ? ", " : "", s.first.c_str());
        }
        for (int b = 0; b < boxes.n; ++b) {
            if (boxes.b[b].bone < 0) continue;
            Mat bm;
            if (turned) bm = game[boxes.b[b].bone];
            else if (!SafeCopy(bm.m, (const void*)(mats + (uintptr_t)boxes.b[b].bone * 64), sizeof bm.m)) continue;
            const Mat w = Mul(bm, xform);
            if (monsterrig::CapsuleOf(w.m, boxes.b[b], &caps[nc])) ++nc;
        }
        {   // a limb bone whose box is off its bone (Akara's thighs and shins under her robe): along the bone, to the farthest bone of the same limb on it
            const auto worldOf = [&](int bone, Mat* out) {
                Mat bm;
                if (turned) bm = game[bone];
                else if (!SafeCopy(bm.m, (const void*)(mats + (uintptr_t)bone * 64), sizeof bm.m)) return false;
                *out = Mul(bm, xform);
                return true;
            };
            for (int o = 0; o < nOffLimb && nc < (int)std::size(caps); ++o) {
                const int bone = offLimb[o].bone;
                Mat w;
                if (!worldOf(bone, &w)) continue;
                int child = -1;
                float far2 = 0.0f;
                for (int cb = 0; cb < count; ++cb) {
                    Mat cw;
                    if (human.parent[cb] != bone || human.limbOf[cb] != human.limbOf[bone] || !worldOf(cb, &cw)) continue;
                    const float d2 = (cw.m[12] - w.m[12]) * (cw.m[12] - w.m[12]) + (cw.m[13] - w.m[13]) * (cw.m[13] - w.m[13]) + (cw.m[14] - w.m[14]) * (cw.m[14] - w.m[14]);
                    if (d2 > far2) { far2 = d2; child = cb; }
                }
                Mat cw, mw;
                if (child < 0 || !worldOf(child, &cw)) continue;
                const int mirror = offLimb[o].mirror;
                const bool haveMirror = mirror >= 0 && mirror < count && worldOf(mirror, &mw);
                const float childAt[3] = {cw.m[12], cw.m[13], cw.m[14]}, mirrorAt[3] = {mw.m[12], mw.m[13], mw.m[14]};
                if (monsterrig::AlongBone(w.m, childAt, haveMirror ? mirrorAt : nullptr, offLimb[o], &caps[nc])) { ++nc; ++r.alongBone; }
            }
        }
        r.n = monsterrig::Keep(caps, nc);
        for (int i = 0; i < r.n; ++i) r.c[i] = caps[i];
        if (!r.n) r.why = "no box made a capsule";
        if (r.n > 0) {   // [weight] monster_ragdoll: a pose the game holds still for a second is a corpse's (vr/corpse_watch.h) - by its largest capsules' ends
            float pts[corpsewatch::kPoints][3];
            int np = 0;
            for (int i = 0; i < r.n && np + 1 < corpsewatch::kPoints; ++i) {
                std::memcpy(pts[np++], r.c[i].a, sizeof pts[0]);
                std::memcpy(pts[np++], r.c[i].b, sizeof pts[0]);
            }
            const ULONGLONG now = GetTickCount64();
            std::lock_guard<std::mutex> lock(g_stillLock);
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};   // ... and lies (corpsewatch::Lying): a body standing at rest is no corpse
            for (int i = 0; i < r.n; ++i)
                for (int q = 0; q < 3; ++q) {
                    lo[q] = std::min(lo[q], std::min(r.c[i].a[q], r.c[i].b[q]) - r.c[i].radius);
                    hi[q] = std::max(hi[q], std::max(r.c[i].a[q], r.c[i].b[q]) + r.c[i].radius);
                }
            const bool still = g_still[c.entity].Look(pts, np, reach / 6.0f, now);   // (reach is 6 m in world units)
            r.still = still && corpsewatch::Lying(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
            if (g_still.size() > 128)
                for (auto it = g_still.begin(); it != g_still.end();) it = now - it->second.seenMs > 3000 ? g_still.erase(it) : std::next(it);
        }
        // [weight] monster_shields: what it holds - its weapon, its shield - as the game builds it (monsterrig::HeldModels): each child of its root that is a
        // held part (RH / LH / SH) or an attached unit's root, and two levels under it the item's model (fallen1.json: the "rhand" part has no model; the
        // item - fallen_scimitar.json - is a unit of its own, a root and a model with no skeleton). A held model bound to this skeleton's bones: its boxes
        // through those bones, as the body's (each read once, cached by its model instance). A rigid item: its meshes' boxes in its own frame
        // (monsterrig::ItemBox), put where the game's own transform of that model's entity has it - within 2 m of the monster, or left out. A weapon a
        // capsule along its box, a shield four across its face (monsterrig::HeldCapsules). The model's log line says for each what it gave, or why not.
        if (heldWanted && ccb) {
            const auto childrenOf = [&](uint32_t e) -> uintptr_t {
                uint32_t ci = 0;
                return ecs::Dense(cs, e, &ci) ? ccb + (uintptr_t)ci * monsterrig::kChildrenCompSize : 0;
            };
            monsterrig::HeldModel hm[MonsterRig::kMaxHeldCaps];
            int looked = 0;
            const int nh = monsterrig::HeldModels(read, childrenOf(c.entity), modelOf, slotOf, childrenOf, hm, MonsterRig::kMaxHeldCaps, &looked);
            r.heldParts = nh;
            int w = 0;
            if (!nh && w < (int)sizeof r.heldSaid - 80)
                w += snprintf(r.heldSaid + w, sizeof r.heldSaid - w, "%d child(ren) of its root could hold something, none with a model under it", looked);
            for (int h = 0; h < nh && r.nHeld < MonsterRig::kMaxHeldCaps; ++h) {
                const bool shield = hm[h].slot == monsterrig::kSH;
                const char* slotName = hm[h].slot == monsterrig::kSH ? "SH" : hm[h].slot == monsterrig::kLH ? "LH" : hm[h].slot == monsterrig::kRH ? "RH" : "attached";
                static std::unordered_map<uintptr_t, monsterrig::Boxes> skinned;   // (the timer's thread only) by the model instance
                auto it = skinned.find(hm[h].inst);
                if (it == skinned.end()) {
                    monsterrig::Boxes hb;
                    monsterrig::BoxesOfModels(read, &hm[h].inst, 1, skelp, &hb);
                    if (!hb.n && std::strcmp(hb.why, "the model is not loaded yet") == 0) {
                        if (w < (int)sizeof r.heldSaid - 40) w += snprintf(r.heldSaid + w, sizeof r.heldSaid - w, "%s%s: loading", w ? "; " : "", slotName);
                        continue;
                    }
                    if (skinned.size() > 128) skinned.clear();
                    it = skinned.emplace(hm[h].inst, hb).first;
                }
                const monsterrig::Boxes& hb = it->second;
                int made = 0;
                if (hb.n) {   // bound to this skeleton's bones: through them
                    for (int b = 0; b < hb.n && r.nHeld < MonsterRig::kMaxHeldCaps; ++b) {
                        const int bone = hb.b[b].bone;
                        if (bone < 0 || bone >= count) continue;
                        Mat bm;
                        if (turned) bm = game[bone];
                        else if (!SafeCopy(bm.m, (const void*)(mats + (uintptr_t)bone * 64), sizeof bm.m)) continue;
                        const Mat bw = Mul(bm, xform);
                        monsterrig::Capsule hc[4];
                        const int k2 = monsterrig::HeldCapsules(bw.m, hb.b[b], shield, hc, std::min(4, MonsterRig::kMaxHeldCaps - r.nHeld));
                        for (int q = 0; q < k2; ++q, ++made) { r.held[r.nHeld] = hc[q]; r.heldKind[r.nHeld] = shield ? 2 : 1; ++r.nHeld; }
                    }
                    if (w < (int)sizeof r.heldSaid - 60) w += snprintf(r.heldSaid + w, sizeof r.heldSaid - w, "%s%s: %d capsule(s), on its bones", w ? "; " : "", slotName, made);
                    continue;
                }
                // a rigid item: its box in its own frame, where the game's transform of its model's entity puts it
                monsterrig::BoneBox box;
                const char* why = "";
                if (!monsterrig::ItemBox(read, hm[h].inst, &box, &why)) {
                    if (w < (int)sizeof r.heldSaid - 80) w += snprintf(r.heldSaid + w, sizeof r.heldSaid - w, "%s%s: no box (%s)", w ? "; " : "", slotName, why);
                    continue;
                }
                // where the game has the item: its transform component's WORLD matrix (+0x40; +0x00 is its local one, against the hand part it hangs on - read
                // as the world's it put the item thousands of metres off: the user's 0.179 run, 'navi.skeleton' "3556.0 m from the monster"), as the hero's own
                // flail is read (FlailStep). The local one only for an item with no parent (its local is the world's).
                uint32_t ti = 0, parentE = 0;
                Mat iw, il;
                const uintptr_t icomp = ecs::Dense(xs, hm[h].entity, &ti) ? xcb + (uintptr_t)ti * kTransformSize : 0;
                const bool haveL = icomp && SafeCopy(il.m, (const void*)icomp, sizeof il.m) && boneguard::Finite(il.m);
                const bool haveW = icomp && SafeCopy(iw.m, (const void*)(icomp + 0x40), sizeof iw.m) && boneguard::Finite(iw.m);
                const bool rooted = icomp && Get(icomp + 0xC0, &parentE) && (parentE & 0xFFFFF) == 0xFFFFF;
                if (!haveL && !haveW) {
                    if (w < (int)sizeof r.heldSaid - 60) w += snprintf(r.heldSaid + w, sizeof r.heldSaid - w, "%s%s: its model has no transform", w ? "; " : "", slotName);
                    continue;
                }
                const float toM = 6.0f / reach;   // (reach is 6 m in world units)
                float apartW = -1.0f, apartL = -1.0f;
                const int which = monsterrig::HeldSpace(haveL ? il.m : nullptr, haveW ? iw.m : nullptr, rooted, c.t, 2.0f / toM, &apartW, &apartL);
                if (apartW >= 0.0f) apartW *= toM;
                if (apartL >= 0.0f) apartL *= toM;
                if (which < 0) {
                    if (w < (int)sizeof r.heldSaid - 110)
                        w += snprintf(r.heldSaid + w, sizeof r.heldSaid - w, "%s%s: not near the monster - world %.1f m, local %.1f m off (parent %05X)", w ? "; " : "", slotName,
                                      apartW, apartL, parentE & 0xFFFFF);
                    continue;
                }
                if (which == 1) iw = il;
                const char* space = which == 0 ? "the world matrix (+0x40)" : "its local matrix (no parent: the world's)";
                const float apartM = which == 0 ? apartW : apartL;
                monsterrig::Capsule hc[4];
                const int k2 = monsterrig::HeldCapsules(iw.m, box, shield, hc, std::min(4, MonsterRig::kMaxHeldCaps - r.nHeld));
                for (int q = 0; q < k2; ++q, ++made) { r.held[r.nHeld] = hc[q]; r.heldKind[r.nHeld] = shield ? 2 : 1; ++r.nHeld; }
                if (w < (int)sizeof r.heldSaid - 120)
                    w += snprintf(r.heldSaid + w, sizeof r.heldSaid - w, "%s%s: %d capsule(s), rigid, %.0f cm long, %.1f m from its root, by %s (local %.1f m off)", w ? "; " : "", slotName,
                                  made, (made ? monsterrig::Extent(r.held[r.nHeld - 1]) : 0.0f) * 100.0f * 6.0f / reach, apartM, space, apartL);
            }
        }
        // [weight] monster_ragdoll: a humanoid by its names must also be seen standing up (vr/monster_humanoid.h Upright: head over hips, the trunk upright, the
        // hands off the ground) kUprightRun reads running - its bones' places in the world now
        if (human.humanoid && !upright && count <= monsterrig::kMaxBones) {
            float pos[monsterrig::kMaxBones][3];
            bool all = true;
            for (int b = 0; b < count && all; ++b) {
                Mat bm;
                if (turned) bm = game[b];
                else if (!SafeCopy(bm.m, (const void*)(mats + (uintptr_t)b * 64), sizeof bm.m)) { all = false; break; }
                const Mat w = Mul(bm, xform);
                pos[b][0] = w.m[12];
                pos[b][1] = w.m[13];
                pos[b][2] = w.m[14];
            }
            if (all) {
                const bool now = monsterhumanoid::Upright(human, pos, count, &pose);
                AcquireSRWLockExclusive(&g_rigLock);
                const auto it = g_rigBoxes.find(key);
                if (it != g_rigBoxes.end()) {
                    it->second.uprightRun = now ? it->second.uprightRun + 1 : 0;
                    it->second.pose = pose;
                    if (it->second.uprightRun >= monsterhumanoid::kUprightRun) it->second.upright = true;
                    upright = it->second.upright;
                }
                ReleaseSRWLockExclusive(&g_rigLock);
            }
        }
        r.doll = !human.humanoid ? -1 : upright ? 1 : 0;
        r.dollWhy = human.why;
        if (all && human.bones == count && r.n > 0) {   // [weight] monster_ragdoll_all: every monster (its skeleton read), not only the humanoids
            r.doll = 1;
            r.dollWhy = r.segments ? "every monster (monster_ragdoll_all), its limbs from its bones" : "every monster (monster_ragdoll_all)";
        }
        if (r.doll > 0 && r.still) { r.doll = 0; r.dollWhy = "lying, its pose held still for a second - a corpse"; }
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) r.m3[row * 3 + col] = xform.m[row * 4 + col];
        if (human.bones == count) std::memcpy(r.parent, human.parent, sizeof(int16_t) * std::min(dollpose::kMaxBones, monsterhumanoid::kMaxBones));
        r.dollPose = pose;
        snprintf(r.dollSaid, sizeof r.dollSaid, "%s", human.said);
        ++k;
    }
    return k;
}

bool HeroTransform(HeroXform* out, const char** why) {
    static const char* const kNoHero = "the hero has not been posed in the last 2 s";
    const uintptr_t self = g_heroSelf.load();
    if (!g_gameBase.load() || !self || GetTickCount64() - g_heroSeen.load() > 2000) { if (why) *why = kNoHero; return false; }
    AcquireSRWLockExclusive(&ecs::g_lock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } unlock;
    if (!ecs::IsStorage(ecs::g_xs, ecs::g_vtX) || !ecs::IsStorage(ecs::g_ss, ecs::g_vtS)) {
        // looked for at most twice a second (a failed look reads ~200 pool entries)
        const ULONGLONG now = GetTickCount64();
        if (now - ecs::g_lastStorageLook < 500) { if (why) *why = ecs::g_why; return false; }
        ecs::g_lastStorageLook = now;
        if (!ecs::FindStorages()) { if (why) *why = ecs::g_why; return false; }
        ecs::g_cachedSelf = 0;
    }
    // The entity: kept while its SkeletonComponent still holds this instance, else looked up.
    uintptr_t held = 0;
    if (ecs::g_cachedSelf != self || !ecs::SkeletonOf(ecs::g_cachedEntity, &held) || held != self) {
        uint32_t e = 0;
        ecs::g_cachedSelf = 0;
        static uintptr_t failSelf = 0;
        static ULONGLONG failAt = 0;
        const ULONGLONG now = GetTickCount64();
        // one pass per half second ([hands] doll_rebuild: per 50 ms - a rebuilt model's instance is in the hero's SkeletonComponent a moment after it is first posed)
        if (self == failSelf && now - failAt < (g_dollRebuild.load(std::memory_order_relaxed) ? 50u : 500u)) { if (why) *why = ecs::g_why; return false; }
        if (!ecs::EntityOf(self, &e)) {
            failSelf = self; failAt = now;
            ecs::g_why = "the hero's SkeletonInstance is in no SkeletonComponent"; if (why) *why = ecs::g_why; return false;
        }
        ecs::g_cachedSelf = self; ecs::g_cachedEntity = e;
    }
    uint32_t i = 0; uintptr_t cb = 0;
    if (!ecs::Dense(ecs::g_xs, ecs::g_cachedEntity, &i) || !Get(ecs::g_xs + 0x38, &cb) || !cb) {
        ecs::g_why = "the hero's entity has no TransformComponent"; if (why) *why = ecs::g_why; return false;
    }
    out->self = self;
    out->entity = ecs::g_cachedEntity;
    out->comp = cb + (uintptr_t)i * kTransformSize;
    out->parent = 0xFFFFFFFFu;
    Get(out->comp + 0xC0, &out->parent);
    return true;
}

void SetFresh(FreshFn fn) { g_fresh.store(fn); }
void SetFreshRoomShift(RoomShiftFn fn) { g_freshShift.store(fn); }
void SetFreshHands(FreshHandsFn fn) { g_freshHands.store(fn); }
void SetGrabHands(GrabHandsFn fn) { g_grabHands.store(fn); }
bool GrabPalmDrawn(int side, float head[3], const char** how) {
    AcquireSRWLockShared(&g_grabPalmLock);
    const bool ok = g_grabPalmAt[side & 1] && GetTickCount64() - g_grabPalmAt[side & 1] < 200;
    for (int k = 0; k < 3; ++k) head[k] = g_grabPalm[side & 1][k];
    if (how) *how = g_grabPalmHow[side & 1];
    ReleaseSRWLockShared(&g_grabPalmLock);
    return ok;
}
void SetGripHandStep(GripHandStepFn fn, GripAbiFn abi) { g_gripHandStep.store(fn); g_gripAbi.store(abi); }

void SetHeroPos(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_heroPosLock);
    g_heroPos[0] = x; g_heroPos[1] = y; g_heroPos[2] = z;
    g_heroPosAt = now;
    if (!g_stillSince || fabsf(x - g_stillAt[0]) > 0.5f || fabsf(z - g_stillAt[1]) > 0.5f) {
        g_stillAt[0] = x; g_stillAt[1] = z; g_stillSince = now;
    }
    ReleaseSRWLockExclusive(&g_heroPosLock);
}

void** OrigSlot() { return (void**)&g_orig; }
void* Detour() { return (void*)&Hook; }

}  // namespace skel
