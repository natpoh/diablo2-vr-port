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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bow_aim.h"
#include "bow_census.h"
#include "bow_hook.h"
#include "bow_hand.h"
#include "bow_rig.h"
#include "bone_guard.h"
#include "bow_touch.h"
#include "grip_fit.h"
#include "grip_pose.h"
#include "skeletons.h"
#include "shield_fist.h"
#include "forearm_twist.h"
#include "view_gate.h"
#include "frame_trace.h"   // [debug] frame_trace: what the pose hook costs a pair
#include "body_calibration.h"
#include "sigscan.h"
#include "weapon_back.h"
#include "weapon_shaft.h"
#include "weight.h"

namespace skel {

namespace {

#pragma optimize("", off)
bool SafeCopy(void* dst, const void* src, size_t n) noexcept {
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma optimize("", on)

template <class T> bool Get(uintptr_t a, T* v) { return a > 0x10000 && SafeCopy(v, (const void*)a, sizeof(T)); }
bool SafeWrite(void* dst, const void* src, size_t n) { return (uintptr_t)dst > 0x10000 && SafeCopy(dst, src, n); }

std::string Str(uintptr_t p) {
    char buf[96];
    int n = 0;
    for (; n < 95; ++n) {
        if (!Get(p + n, &buf[n])) return {};
        if (!buf[n]) break;
        if (buf[n] < 32 || buf[n] > 126) return {};
    }
    return std::string(buf, n);
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
    // two_hand_weapon_attach (the barbarian's rig, under the body, not a wrist): the game hangs a
    // two-handed weapon on it in some animations - walking with a great axe (2026-10-06).
    int twoHand = -1;
    std::vector<char> upper;    // spine_01 and everything under it: the body above the pelvis
    int upr[2] = {-1, -1}, lwr[2] = {-1, -1}, hand[2] = {-1, -1};   // [0] right, [1] left
    // first knuckles: index, middle, pinky - they give the hand's own axes
    int ind[2] = {-1, -1}, mid[2] = {-1, -1}, pnk[2] = {-1, -1};
    int head = -1, spine = -1, pelvis = -1;
    int eyes[2] = {-1, -1};     // eyeball bones, where the rig has them
    // Where this hero's eyes are, standing (model space y): the camera's height
    // per class. And the pelvis standing: how far above it the animation lifts
    // the hips is a jump (the barbarian's Leap), the camera goes up with it.
    float eyeY = 0.0f, pelvisY = 0.0f;
    // The bind pose (model space, from granny_bone::InverseWorld4x4 at +0x50),
    // what the body above the pelvis is held in instead of the animation.
    std::vector<Mat> bind;
    std::vector<char> inHand;   // under a hand (fingers, weapon): keeps the animation's grip
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
#if D2RVR_FIRST_PERSON
// [gameplay] shield_smite: the left weapon attach bone in the render world, as last posed (only while asked for)
ShieldPose g_shieldPose{};
ULONGLONG g_shieldAt = 0;
std::atomic<ULONGLONG> g_shieldWantedAt{0};
SRWLOCK g_twistNoteLock = SRWLOCK_INIT;
std::string g_twistNote;                // [hands] forearm_twist: the last rig's twist bones, for the log
std::atomic<int> g_shieldStrapped{0};   // [shield] forearm: 1 strapped this pose, -1 asked but not (hidden, no bind pose), 0 not asked
#endif
#if D2RVR_FIRST_PERSON
std::atomic<int> g_gunAxis{0};   // GunAxis
struct GunBoneOut { float o[3], ax[3][3]; ULONGLONG at; };
SRWLOCK g_gunBoneLock = SRWLOCK_INIT;
GunBoneOut g_gunBone{};
// The posed weapon in world units (PosedWeapon, melee by touch); written by NotePosedWeapon at the end of a pose.
SRWLOCK g_posedLock = SRWLOCK_INIT;
PosedWeaponPose g_posed{};
ULONGLONG g_posedAt = 0;
// [debug] the line the left hand takes the weapon by (staff, spear, sword), the same frame
struct GrabLineOut { float a[3], b[3]; ULONGLONG at; };
GrabLineOut g_grabLine{};
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
std::map<std::pair<uintptr_t, uintptr_t>, Hero> g_heroes;   // count == 0: not the hero

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
};
SRWLOCK g_weightLock = SRWLOCK_INIT;
WeightRel g_weightRel;
#endif

SRWLOCK g_inLock = SRWLOCK_INIT;
Input g_in;
std::atomic<FreshFn> g_fresh{nullptr};
std::atomic<FreshHandsFn> g_freshHands{nullptr};

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
constexpr uint32_t kTransformSize = 0xD0, kSkeletonCompSize = 0x10;

std::atomic<uintptr_t> g_gameBase{0};
std::atomic<bool> g_trackHero{false};      // vrcam wants the hero known even while no pose is changed
std::atomic<uintptr_t> g_heroSelf{0};      // the hero's SkeletonInstance, last posed
std::atomic<uint32_t> g_heroGen{0};        // bumped when it changes (a weapon swap rebuilds his model)
std::atomic<ULONGLONG> g_heroSeen{0};

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
bool BodyPlace(uintptr_t self, float* x, float* z) {
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
        if (it != g_bodyEntity.end() && it->second.first == kNoEntity && now - it->second.second < 500) return false;
        if (!EntityOf(self, &e)) e = kNoEntity;
        if (g_bodyEntity.size() > 256) g_bodyEntity.clear();
        g_bodyEntity[self] = {e, now};
        if (e == kNoEntity) return false;
    }
    uint32_t i = 0, parent = kNoEntity;
    uintptr_t cb = 0;
    if (!Dense(g_xs, e, &i) || !Get(g_xs + 0x38, &cb) || !cb) return false;
    const uintptr_t comp = cb + (uintptr_t)i * kTransformSize;
    if (!Get(comp + 0xC0, &parent)) return false;
    float t[3];
    if (!SafeCopy(t, (const void*)(comp + ((parent & 0xFFFFF) == 0xFFFFF ? 0x00 : 0x40) + 12 * 4), sizeof t)) return false;
    if (!std::isfinite(t[0]) || !std::isfinite(t[2])) return false;
    *x = t[0]; *z = t[2];
    return true;
}
}  // namespace ecs

bool Learn(uintptr_t self, Hero* h) {
    uintptr_t modelInst = 0, skel = 0, nameObj = 0, nameP = 0;
    if (!Get(self + 0x08, &modelInst) || !Get(modelInst + 0x08, &skel)) return false;
    Get(self, &nameObj); Get(nameObj + 8, &nameP);
    const std::string model = Str(nameP);
    const size_t at = model.find("/character/player/");
    if (at == std::string::npos) return false;
    h->cls = -1;
    for (int c = 0; c < 8; ++c)
        if (model.compare(at + 18, strlen(kHeroClasses[c]) + 1, std::string(kHeroClasses[c]) + "/") == 0) h->cls = c;
    if (h->cls >= 0) g_heroClass.store(h->cls);   // known as soon as the hero's skeleton is learnt, in whatever view (the per-class wrist depth and D2R VR Settings read it)
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
bool GunFrame(const Input& in, float c, float s, V3* F, V3* U, V3* R) {
    const float* cq = in.rot[0];
    Q4 q = {cq[0], cq[1], cq[2], cq[3]};
    const float ql = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (!(ql > 0.5f)) return false;
    q = {q.x/ql, q.y/ql, q.z/ql, q.w/ql};
    const V3 f = QRot(q, {0, -1, 0}), u = QRot(q, {0, 0, -1}), r = QRot(q, {1, 0, 0});
    const float a[3] = {f.x, f.y, f.z}, b[3] = {u.x, u.y, u.z}, d[3] = {r.x, r.y, r.z};
    *F = Norm(ToModel(a, c, s)); *U = Norm(ToModel(b, c, s)); *R = Norm(ToModel(d, c, s));
    return true;
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

// One arm's two-bone IK, the wrist to T: elbow down, a little out to its own
// side and back - of the body as turned.
void Reach(const Hero& h, int side, V3 T, bool turn, float c, float s, const std::vector<Mat>& baseW, std::vector<Mat>& newW) {
    const int u = h.upr[side], l = h.lwr[side], k = h.hand[side];
    const V3 S = Pos(newW[u]);
    const float L1 = Len(Sub(Pos(newW[l]), S)), L2 = Len(Sub(Pos(newW[k]), Pos(newW[l])));
    V3 pole = {side == 0 ? -0.5f : 0.5f, -1.0f, -0.3f};
    if (turn) pole = {pole.x * c + pole.z * s, pole.y, -pole.x * s + pole.z * c};
    const V3 E = Elbow(S, L1, L2, T, pole);
    AimChain(h, u, l, k, E, T, baseW, newW);
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
bool StrapShield(const Hero& h, const Input& in, const std::vector<Mat>& oldW, std::vector<Mat>& newW, float roll) {
    const int n = h.count, wb = h.weapon[1], l = h.lwr[1], k = h.hand[1];
    if (wb < 0 || l < 0 || k < 0 || !h.bindOk) return false;
    const Mat anim = Mul(oldW[wb], InvAffine(oldW[k]));
    if (Len({anim.m[0], anim.m[1], anim.m[2]}) < 0.3f) return false;   // hidden by the game
    const Mat local = Mul(h.bind[wb], InvAffine(h.bind[l]));
    const float size = Len({local.m[0], local.m[1], local.m[2]});
    if (!(size > 0.3f && size < 3.0f)) return false;
    Mat arm = newW[l];
    const V3 E = Pos(newW[l]), W = Pos(newW[k]);
    if (roll != 0.0f && in.forearmTwist >= 0.0f && Len(Sub(W, E)) > 1e-4f) {
        const V3 bE = Pos(h.bind[l]), bEW = Sub(Pos(h.bind[k]), bE);
        const float ll = Dot(bEW, bEW);
        const float t = ll > 1e-8f ? std::clamp(Dot(Sub(Pos(h.bind[wb]), bE), bEW) / ll, 0.0f, 1.0f) : 1.0f;
        TurnAboutSelf(arm, TurnAbout(Norm(Sub(W, E)), (forearmtwist::Share(in.forearmTwist, t) - forearmtwist::Share(in.forearmTwist, 0.0f)) * roll));
    }
    const std::vector<Mat> before = newW;
    newW[wb] = Mul(local, arm);
    const float* a = in.shieldAdj;
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
    }
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

// The hero's forearms as drawn, for FlatVR (the toolbar lies on one): the
// elbow (forearm bone) and the wrist (hand bone), and the way the palm faces,
// square to the arm - it turns as the forearm twists, not as the wrist bends. In BodyWalk's hand
// frame: metres from the head, x right, y up, z back, head yaw only (the
// frame the controllers came in by), so FlatVR lays them back where they were
// drawn. [0] right, [1] left.
// palm: the palm's middle (wrist to middle knuckle, halfway, 3 cm out of the palm) - what a belt potion is held by
struct ForearmOut { float elbow[3], wrist[3], across[3], palm[3], palmModel[3]; bool palmOk; ULONGLONG at;   // palmModel: in his model space
                    float elbowW[3], wristW[3], acrossW[3], pointW[3], faceW[3]; bool worldOk, handW; ULONGLONG worldAt; };   // and in the WORLD (HeroArmWorld), made at the pose at worldAt
std::atomic<const char*> g_armWorldWhy{"not yet posed"};   // why the last pose could not put the arm in the world (HeroArmWhy); "" while it could

SRWLOCK g_forearmLock = SRWLOCK_INIT;
ForearmOut g_forearm[2] = {};

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
// The wrist the bow hangs on for the bow's rig and the aim: BowSideOf's when the attach bones tell, else the hand opposite the string
// hand the draw detector names, else the one opposite vrcam's string side (Input::bowStringSide - the bow is in the aim hand).
// An Amazon's quiver sits in the other slot and both attach bones are shown, so the bones alone never told hers. `sizes` get the bones'.
bowaim::BowWrist BowWristNow(const Hero& h, const Input& in, const std::vector<Mat>& oldW, float sizes[2]) {
    sizes[0] = sizes[1] = -1.0f;
    BowSideOf(h, oldW, sizes);
    bool drawing = false;
    const int fed = BowFeedSide(&drawing);
    return bowaim::ChooseBowWrist(sizes[0], sizes[1], fed, in.bowStringSide == 0 || in.bowStringSide == 1 ? in.bowStringSide ^ 1 : -1);
}
// The final attach bone `w` (model space, as ours left it) into the game's world: the hero's own model-to-world
// (his TransformComponent's local matrix, set before this very pose - a later copy displaced fast swings) times the
// bone. Kept with the stamp of the hand sample this pose is made from, so a swing is matched to the hand that posed
// it. Idea and shape after bearstonem/d2r-pcvr (MIT) skeletons.cpp MeleeWeapon. Read-only on the game.
void NotePosedWeapon(uintptr_t self, const Mat& w, int side, const Input& in, const PosedBlade (&blades)[2]) {
    PosedWeaponPose out;
    out.blade[0] = blades[0]; out.blade[1] = blades[1];
    bool ok = false;
    HeroXform hx; Mat model;
    if (in.havePoseStamp && HeroTransform(&hx) && hx.self == self && (hx.parent & 0xFFFFFu) == 0xFFFFFu &&
        SafeCopy(model.m, (const void*)hx.comp, sizeof model)) {
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

// [bow] string_anchor: the bow's string is held at the first joints of the hooked fingers, not at the controller's grip point (the middle of
// the palm, about a hand's length behind them - the string ran past the back of the hand and hooked nothing). The three joints (index, middle,
// ring: vr/bow_hook.h kAnchorJoint) as HookPose puts them on the string hand's wrist as it stands now - the fingers sit on the wrist rigidly,
// so this does not wait for them to be posed, which comes later in the pose - moved by the sliders along the hand's own axes. The Amazon's
// right hand only, with hook_game_pose; anything else keeps the grip point.
bowhook::Tune HookTune(const Input& in) {
    bowhook::Tune t;
    t.curl = in.bowHookCurl;
    t.thumbDeg = in.bowHookThumbDeg;
    return t;
}

bool HookAnchor(const Hero& h, int side, const Input& in, const std::vector<Mat>& newW, float upm, V3* out, const char** why) {
    static thread_local std::vector<Mat> scratch;   // (HookPose writes the finger bones only; nothing else of it is read)
    if (scratch.size() != newW.size()) scratch.assign(newW.size(), Mat{});
    if (!HookPose(h, side, newW, scratch, HookTune(in))) { *why = "the hooked pose is not known for this hand (the Amazon's right hand only)"; return false; }
    bowhook::Vec joints[3];
    int count = 0;
    for (const Hero::Finger& f : h.fingers[side])
        if (bowhook::InAnchor(f.kind) && (int)f.bones.size() > bowhook::kAnchorJoint && count < 3) {
            const V3 p = Pos(scratch[f.bones[bowhook::kAnchorJoint]]);
            joints[count++] = {p.x, p.y, p.z};
        }
    const int k = h.hand[side], m = h.mid[side], i = h.ind[side], p = h.pnk[side];
    if (count < 1 || k < 0 || m < 0 || i < 0 || p < 0) { *why = "the fingers' joints were not found"; return false; }
    const auto v = [&](int bone) { const V3 q = Pos(newW[bone]); return bowhook::Vec{q.x, q.y, q.z}; };
    const bowhook::Frame frame = bowhook::HandFrame(v(k), v(m), v(i), v(p), side);
    bowhook::Vec point;
    if (!bowhook::AnchorPoint(joints, count, frame, in.bowAnchorCm[0], in.bowAnchorCm[1], in.bowAnchorCm[2], upm, &point)) { *why = "the hand has no frame"; return false; }
    *out = {point.x, point.y, point.z};
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

StringAnchor StringAnchorNow(const Hero& h, const Input& in, const std::vector<Mat>& oldW, const std::vector<Mat>& newW, float c, float s) {
    StringAnchor a;
    if (!in.bowStringAnchor) return a;
    if (!in.bowHookGame || !in.bowHandPose) { a.why = "needs the string hand's pose and the game's draw pose (hand_pose, hook_game_pose)"; return a; }
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
    const bool have = HookAnchor(h, side, in, newW, upm, &joint, &why);
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
    if (bowSide < 0 || h.head < 0 || h.weapon[bowSide] < 0 || h.hand[bowSide] < 0) {
        st.why = bowSide < 0 ? "the wrist the bow hangs on is not known (the attach bones cannot tell, and neither the draw feed nor the aim hand)" : "the hero's wrist bones were not found";
        publish();
        return;
    }
    if ((in.handsValid & 3u) != 3u) { st.why = "a controller is not tracked"; publish(); return; }
    float af[3], au[3], as[3];
    if (!BowAxesNow(af, au, as)) { st.why = "the bow's own axes are not read yet (its rig is not a known layout, or not the hero's)"; publish(); return; }
    const int wb = h.weapon[bowSide], k = h.hand[bowSide];
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
    if (in.bowRest) {
        AcquireSRWLockExclusive(&g_bowAimLock);
        restFade = g_restBlend.StepSparse(drawing, now, in.bowAimBlendMs);
        ReleaseSRWLockExclusive(&g_bowAimLock);
        offsetCur = bowaim::OffsetIn(cur, in.bowRestCm[0], in.bowRestCm[1], in.bowRestCm[2]) * (0.01f * upm * restFade);
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
        char more[120];
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

void PoseBody(uintptr_t self, const Hero& h, const Input& in, const weight::Out& wo) {
    uintptr_t world = 0, mats = 0; int32_t n = 0;
    if (!Get(self + 0x20, &world) || !Get(world, &n) || !Get(world + 4, &mats) || n != h.count) return;
    std::vector<Mat> oldW(n);
    if (!SafeCopy(oldW.data(), (const void*)mats, sizeof(Mat) * n)) return;
    std::vector<Mat> newW = oldW;
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
    if (in.lockUpper && h.bindOk) {
        for (int i = 0; i < n; ++i) {
            if (!h.upper[i]) continue;
            const int p = h.parent[i];
            if (i == h.spine || p < 0) { newW[i] = h.bind[i]; newW[i].m[13] += lift; }   // up with the hips in a jump
            else if (h.inHand[i]) newW[i] = Mul(Mul(oldW[i], InvAffine(oldW[p])), newW[p]);
            else newW[i] = Mul(Mul(h.bind[i], InvAffine(h.bind[p])), newW[p]);
        }
        // [body] legs_under_body: the trunk now stands in its bind place, but
        // the hips are still the game's - and a blow lunges them ahead and
        // twists them (the barbarian's legs jumped out in front of him on
        // every swing). Everything below the trunk is moved back so the pelvis
        // stands where the bind pose has it across the floor, and turned so it
        // faces the bind pose's way. Its height stays the animation's (a
        // crouch, a jump), and the legs still step under it.
        if (in.legsUnder && h.pelvis >= 0) {
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
    // A measured arm span (T-pose, [calibration] use=1; vr/body_calibration.h): each arm's bones, upper arm and forearm, are
    // scaled about their shoulder until the wrist stands where the controller's grip puts it at the world's scale - the hands
    // then reach as far as the real ones and the controllers stay 1:1. The hand, its fingers and what it holds are put back
    // at their authored size on the new wrist. Presentation only: no bone length the game reads, only the drawn ones.
    // The idea is bearstonem/d2r-pcvr's (MIT, vr/skeletons.cpp PoseBody).
    if (in.mode == 2 && in.armSpanM > 0.0f && h.bindOk && h.upr[0] >= 0 && h.upr[1] >= 0) {
        // the same eye height the hands are placed by (ControllerPoint)
        const float modelEye = in.haveEye ? in.eyeModel[1] : h.head >= 0 ? Pos(newW[h.head]).y : h.eyeY;
        const float shoulderWidth = Len(Sub(Pos(h.bind[h.upr[0]]), Pos(h.bind[h.upr[1]])));
        for (int side = 0; side < 2; ++side) {
            const int u = h.upr[side], l = h.lwr[side], k = h.hand[side];
            if (u < 0 || l < 0 || k < 0) continue;
            const float length = Len(Sub(Pos(h.bind[l]), Pos(h.bind[u]))) + Len(Sub(Pos(h.bind[k]), Pos(h.bind[l])));
            const float palm = h.mid[side] >= 0 ? 0.5f * Len(Sub(Pos(h.bind[h.mid[side]]), Pos(h.bind[k]))) : 0.0f;   // the wrist is that far behind the grip
            const float fit = bodycal::ArmScale(modelEye, in.userHeadM, in.armSpanM, shoulderWidth, length, palm);
            if (fit == 1.0f) continue;
            const std::vector<Mat> before = newW;
            const V3 shoulder = Pos(newW[u]);
            std::vector<char> subtree(n, 0);
            subtree[u] = 1;
            for (int i = u; i < n; ++i) {
                if (i != u && h.parent[i] >= 0) subtree[i] = subtree[h.parent[i]];
                if (subtree[i]) bodycal::ScaleAbout(newW[i].m, {shoulder.x, shoulder.y, shoulder.z}, fit);
            }
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col) newW[k].m[row * 4 + col] = before[k].m[row * 4 + col];   // the hand's own size and turn
            std::vector<char> fromHand(n, 0);
            fromHand[k] = 1;
            Propagate(h, before, newW, fromHand, k);   // fingers, weapon bones: their offsets from the hand as they were
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
    if (gripWanted && !in.attacking && h.weapon[0] >= 0 && h.weapon[1] >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
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
        static Grip goodXbow;
        if (in.gunStock && Len(Sub(pw[0], pw[1])) > 1.0f) {
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
    if (wo.on && in.mode == 2 && in.weight.gripFit && in.weight.gripFingers && !in.attacking && h.bindOk) {
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
    const bool freeStaff = in.staff && in.staffFreeLeft && in.mode == 2 && in.holdGrip && grip.ok && (in.handsValid & 1u) &&
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
    const bool gun = freeStaff && in.gunStock && in.gunFrame && h.hand[0] >= 0 && GunFrame(in, c, s, &gunF, &gunU, &gunR);
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
        gunAt = ControllerPoint(h, 0, in, newW, c, s);
        const V3 at = Add(gunAt, Add(Scale(gunR, a[0] * 0.01f * upm), Add(Scale(gunU, a[1] * 0.01f * upm), Scale(gunF, a[2] * 0.01f * upm))));
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
    StringAnchor anchor;
    if (in.bowStringAnchor && in.mode == 2 && (in.bowAim || in.bowAxis || in.bowRest || in.bowRig || in.bowString)) anchor = StringAnchorNow(h, in, oldW, newW, c, s);
    if ((in.bowAim || in.bowAxis || in.bowRest) && in.mode == 2 && in.holdGrip && grip.ok && !gun) AimBow(h, in, oldW, baseW, newW, c, s, turn, n, anchor);
    bool weightDrew = false;     // [weight]: the weight block below set the weapon bones (both attach bones are W): nothing after moves them
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
        Mat bodyM{};
        {
            Q4 q = {wo.bodyRot[0], wo.bodyRot[1], wo.bodyRot[2], wo.bodyRot[3]};
            const float ql = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            q = ql > 0.5f ? Q4{q.x / ql, q.y / ql, q.z / ql, q.w / ql} : Q4{0, 0, 0, 1};
            const V3 axes[3] = {QRot(q, {1, 0, 0}), QRot(q, {0, 1, 0}), QRot(q, {0, 0, 1})};
            for (int r3 = 0; r3 < 3; ++r3) {
                const float v[3] = {axes[r3].x, axes[r3].y, axes[r3].z};
                const V3 a = ToModel(v, c, s);
                bodyM.m[r3 * 4] = a.x; bodyM.m[r3 * 4 + 1] = a.y; bodyM.m[r3 * 4 + 2] = a.z;
            }
            const float hv[3] = {wo.body[0] + in.handOffset[0], wo.body[1] + in.handOffset[1], wo.body[2] + in.handOffset[2]};
            const V3 p = Add(headM, Scale(ToModel(hv, c, s), upmM));
            bodyM.m[12] = p.x; bodyM.m[13] = p.y; bodyM.m[14] = p.z; bodyM.m[15] = 1.0f;
        }
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
            if (weaponback::BackMatrix(bind, now, cur, in.weight.tipAxis, wo.midM, upmM / std::max(0.1f, in.scale),
                                       in.weight.backLong ? weaponback::Class::Long : weaponback::Class::Short, t, &placed)) {
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
            if (b < 0 || (side != slot && !in.bothAttach)) continue;   // the other wrist's own (a shield) stays
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
        if (in.weight.gripFit && !wo.free && !onBack) {
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
            bool baseKnown = false;
            AcquireSRWLockShared(&g_weightLock);
            if (g_weightRel.baseOk && g_weightRel.baseType == in.weaponType) { baseRel = g_weightRel.base; baseKnown = true; }
            ReleaseSRWLockShared(&g_weightLock);
            bool moved = false;
            for (int pass = 0; pass < 2 && ax >= 0 && ax <= 2; ++pass) {   // the base hand first (its relation is what the others copy), then the hands that took it
                for (int side = 0; side < 2; ++side) {
                    const int k = h.hand[side], mk = h.mid[side], ik = h.ind[side], pk = h.pnk[side];
                    if (k < 0 || mk < 0 || ik < 0 || pk < 0 || !wo.holds[side] || !(wo.on01[side] > 0.0f) || !(in.handsValid & (1u << side))) continue;
                    const bool isBase = wo.grabs[side] == 0;
                    if ((pass == 0) != isBase) continue;
                    gripfit::Tuning user;
                    for (int i = 0; i < 3; ++i) { user.off[i] = in.weight.gripHand[side][i]; user.rot[i] = in.weight.gripHand[side][3 + i]; }
                    const gripfit::Plan plan = gripfit::PlanFor(wo.grabs[side], user, in.weight.gripLikeBase, baseKnown ? &baseRel : nullptr);
                    gripfit::Out fo;
                    if (plan.act) {
                        const gripfit::Hand gh = handGeom(side);
                        fo = gripfit::Solve(gh, gs, plan.tune, plan.mode, ucm);
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
                    if (isBase && wo.on01[side] >= 1.0f) {   // the base hand as it is drawn now (its corrections in): how it sits on the shaft - what the hands that take it copy
                        bool okRel = false;
                        const gripfit::Tuning rel = gripfit::InCm(gripfit::Relation(handGeom(side), gs, &okRel), ucm);
                        if (okRel && std::fabs(rel.off[1]) < 20.0f && std::fabs(rel.off[2]) < 20.0f) {   // (a hand 20 cm off the axis is no base grip)
                            baseRel = rel;
                            baseKnown = true;
                            AcquireSRWLockExclusive(&g_weightLock);
                            g_weightRel.base = rel;
                            g_weightRel.baseOk = true;
                            g_weightRel.baseType = in.weaponType;
                            ReleaseSRWLockExclusive(&g_weightLock);
                        }
                    }
                }
            }
            if (moved)
                for (int side = 0; side < 2; ++side) {
                    const int b = h.weapon[side];
                    if (b < 0 || (side != slot && !in.bothAttach)) continue;
                    newW[b] = attachFor(side, W);   // where it was
                    std::vector<char> fromWeapon(n, 0);
                    fromWeapon[b] = 1;
                    Propagate(h, oldW, newW, fromWeapon, b);
                }
        }
        // The weight model's shaft IS the drawn weapon bone: W, just set on both attach bones, measured against this body frame on every pose it is held (vr/weapon_shaft.h) and
        // told back (weight::SetDrawnShaft) - whichever wrist the game would hang the item on, whichever hand carries it. So the red line, the line both grasp points lie
        // on and hit by touch's capsule (built from the same bone, with the same ends) are one line. (It was measured at the end of the pose from the bone of the game's
        // hand slot only while that slot's bone was the drawn one: walking, the game moves a two-handed weapon to the other wrist, and the model's line then stood still
        // while the weapon moved. The old carry of the other attach bone, below, no longer runs for a weapon the weight model holds: it shifted that bone along the shaft.)
        if (!wo.free) {
            if (in.weight.shaftTipM + in.weight.shaftButtM > 0.01f) {
                const weaponshaft::Line ln = weaponshaft::Relative(W.m, bodyM.m, upmM, in.weight.tipAxis, in.weight.shaftTipM, in.weight.shaftButtM);
                if (ln.ok) {
                    const float a[3] = {(float)ln.at[0], (float)ln.at[1], (float)ln.at[2]}, d[3] = {(float)ln.dir[0], (float)ln.dir[1], (float)ln.dir[2]};
                    weight::SetDrawnShaft(a, d, (float)ln.butt, (float)ln.tip, (float)ln.boneAlong);
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
    if (freeStaff) {
        const int wb = h.weapon[staffSide], lk = h.hand[1], rk = h.hand[0];
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
            if (in.handsValid & 2u) {
                const V3 C = ControllerPoint(h, 1, in, newW, c, s);
                const float headY = in.haveEye ? in.eyeModel[1] : Pos(newW[h.head]).y;
                const float upm = std::clamp(headY / std::max(0.8f, in.userHeadM), 2.0f, 8.0f) * in.scale;   // model units per metre
                // a crossbow is taken at its one place only (leftStaffAtM), a sword's hilt
                // about its one place (leftHilt), a staff anywhere along it
                const float t = in.leftStaffAtM >= 0.0f ? tR + in.leftStaffAtM * upm
                              : in.leftHilt ? tR + std::clamp(Dot(Sub(C, origin), axis) - tR, (in.leftHiltAtM - in.leftHiltSlideM) * upm,
                                                              (in.leftHiltAtM + in.leftHiltSlideM) * upm)
                                            : std::clamp(Dot(Sub(C, origin), axis), tR - kStaffReach * gap, tR + kStaffReach * gap);
                inReach = Len(Sub(C, Add(origin, Scale(axis, t)))) <= in.staffGrabM * upm;
            }
            const float e = (in.handsValid & 2u) ? StaffEase(in.grip[1], inReach, in.fistCap[1] >= 0.0f) : 1.0f;
            leftOnStaff = e >= 0.5f;
            // Held in both hands, the staff is held between them: it runs from
            // the right fist through the left controller, and the right wrist's
            // own turn no longer swings it. Turned about the shaft's point in the
            // right fist, the right hand with it (so the fist stays round the
            // shaft; its roll about the shaft is still the controller's).
            // Not a crossbow: it points where the right controller does, the left hand only holds it.
            // Nor a two-handed sword: the hands sit a fist apart on its hilt, and the line between
            // the controllers that close turned the blade with every tremor.
            if (!gun && !in.leftHilt && e > 0.0f && (in.handsValid & 2u) && rk >= 0) {
                const V3 pR = Add(origin, Scale(axis, tR));
                V3 toL = Sub(ControllerPoint(h, 1, in, newW, c, s), pR);
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
                if (in.handsValid & 2u)
                    along = std::clamp(Dot(Sub(ControllerPoint(h, 1, in, newW, c, s), origin), axis) - tR,
                                       (in.leftHiltAtM - in.leftHiltSlideM) * upm, (in.leftHiltAtM + in.leftHiltSlideM) * upm);
            } else if (in.handsValid & 2u) {
                along = std::clamp(Dot(Sub(ControllerPoint(h, 1, in, newW, c, s), origin), axis) - tR, -kStaffReach * gap, kStaffReach * gap);
                if (fabsf(along) < 0.3f * gap) along = along < 0.0f ? -0.3f * gap : 0.3f * gap;
            }
            const V3 onShaft = Add(origin, Scale(axis, tR + along));
            // The hand on it: turned only so that the line through its fist lies
            // along the shaft; its roll about the shaft stays the controller's -
            // whole, the fist went round the shaft whenever the right hand
            // twisted the staff. Then moved so the fist's middle (the staff's
            // origin in the game's grip) sits on the shaft.
            const Mat inHand = InvAffine(grip.inStaff[1]);   // staff frame -> the left hand's own
            V3 alongInHand = RotOnly(shaftInStaff, inHand), palmInHand = Through(inHand, {0.0f, 0.0f, 0.0f});
            const Mat freeHand = newW[lk];
            // A two-handed sword hangs on the RIGHT wrist and the game's animation keeps the left
            // hand off it: the bone's origin is in the right fist, half a metre from the left one,
            // and the left hand stood that far off the hilt (2026-10-06). The left hand's own fist
            // instead: the hilt runs through it along the knuckles, index -> little finger (toward
            // the pommel, below the right fist), through the palm's middle (wrist to middle knuckle).
            // The same for every shaft laid by its bone's axis (spear, polearm, axe, mace): the game's
            // left hand is not on it either.
            if ((in.leftHilt || (in.shaftAxis != 0 && !gun)) && h.ind[1] >= 0 && h.pnk[1] >= 0 && h.mid[1] >= 0) {
                const Mat toHand = InvAffine(freeHand);
                alongInHand = RotOnly(Sub(Pos(newW[h.pnk[1]]), Pos(newW[h.ind[1]])), toHand);
                palmInHand = Through(toHand, Scale(Add(Pos(freeHand), Pos(newW[h.mid[1]])), 0.5f));
            }
            Mat onStaff = freeHand;
            TurnAboutSelf(onStaff, RotBetween(RotOnly(alongInHand, freeHand), axis));
            // [hands] xbow_left_roll: turned about the shaft on top (a crossbow's stock
            // is held from below, palm up - the controller's own roll left it on the side).
            if (const float deg = in.leftStaffRollDeg; deg != 0.0f) {
                const V3 k = axis;
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
                Reach(h, 1, Add(F, Scale(Sub(O, F), e)), turn, c, s, baseW, newW);
                Mat m = BlendRot(freeHand, onStaff, e);
                m.m[12] = newW[lk].m[12]; m.m[13] = newW[lk].m[13]; m.m[14] = newW[lk].m[14];
                newW[lk] = m;
                std::vector<char> fromHand(n, 0);
                fromHand[lk] = 1;
                Propagate(h, oldW, newW, fromHand, lk);   // the fingers
            }
            // The staff where the hands put it: the left wrist re-derived it
            // (the game hangs it under that wrist).
            newW[wb] = staff;
            std::vector<char> fromStaff(n, 0);
            fromStaff[wb] = 1;
            Propagate(h, oldW, newW, fromStaff, wb);
            // For the aim: the shaft from the left hand's side toward the right
            // one, back in the head's turn-only frame (ToModel undone).
            const V3 d = Scale(axis, -1.0f);
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
    } else if (in.staff && in.mode == 2 && (in.handsValid & 3u) == 3u) {
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
            V3 across{0, 0, 0};
            if (const int i = h.ind[side], p = h.pnk[side]; i >= 0 && p >= 0) {
                // The way the palm faces, square to the forearm: from the
                // knuckles' line (pinky -> index), which a bending wrist turns
                // about and so leaves alone - only the forearm's own twist
                // turns it. As Fist works the palm out, the forearm in place
                // of wrist -> middle knuckle. (The hand's axis most square to
                // the forearm, picked afresh each frame, swapped for another
                // as the wrist bent: the toolbar jumped onto the arm's edge.)
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
    bool leftFisted = false;   // the free-hand rule closed the left hand in this pose
    // [weight] grip_fit_fingers (vr/grip_pose.h): the hand that took the weapon has the game's own weapon-grip pose of the fingers and the thumb - the pose of the hand the game holds it
    // in, read above, put on this hand in its own frame - instead of closing with the squeeze: fixed while it holds the weapon, easing as it takes it and lets go. Until it is
    // fully on (poseE < 1) the fist rule below still draws the hand it comes from (or goes to); fully on, the fist rule leaves it alone. A pose that cannot be put on (the rig's two
    // hands do not answer each other, no pose read yet for this weapon): the fingers close with the squeeze as before.
    float poseE[2] = {0.0f, 0.0f};
    std::vector<Mat> poseW;   // the pose as it would stand: a copy of newW with the taking hand's finger bones set
    if (wo.on && in.mode == 2 && in.weight.gripFit && in.weight.gripFingers && h.bindOk) {
        GripPoseKept kept;
        AcquireSRWLockShared(&g_gripPoseLock); kept = g_gripPose; ReleaseSRWLockShared(&g_gripPoseLock);
        static int told[2] = {0, 0};   // 0 nothing said, 1 the pose took, 2 it could not (said once for each change)
        for (int side = 0; side < 2; ++side) {
            const float t = std::clamp(wo.on01[side], 0.0f, 1.0f);
            if (side == wo.slot || !(t > 0.0f) || (side == 1 && leftOnStaff)) continue;   // (the slot's hand has the game's pose; staff_free_left's left keeps the game's grip)
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
    if ((in.fist || (in.weight.gripFit && wo.on)) && in.mode == 2)
        for (int side = 0; side < 2; ++side) {
            if (poseE[side] >= 1.0f) continue;   // fully in the game's grip pose: the squeeze does not drive it
            const bool onWeapon = wo.on && wo.holds[side] && side != wo.slot;
            const bool free = wo.on ? (side == wo.slot ? !wo.holds[side] : in.handFree[side] || onWeapon) : in.handFree[side];
            if (!in.fist && !onWeapon) continue;   // (fist off: only a hand on the weapon, by grip_fit)
            if (free && (in.handsValid & (1u << side)) && (in.grip[side] >= 0.0f || onWeapon) && !(side == 1 && leftOnStaff)) {   // on a shaft: the game's grip, not our fist (it wraps better, 2026-10-06)
                const float squeeze = in.weight.haveRawGrip ? in.weight.rawGrip[side] : in.grip[side];
                Fist(h, side, onWeapon ? (in.weight.gripFit ? gripfit::Close(squeeze) : 1.0f) : in.grip[side],
                     in.fistCap[side] >= 0.0f ? std::min(in.fistClose[h.cls >= 0 ? h.cls : 3], in.fistCap[side]) : in.fistClose[h.cls >= 0 ? h.cls : 3],
                     in.fistOpen[h.cls >= 0 ? h.cls : 3], oldW, newW);
                if (side == 1) leftFisted = true;
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
        if (side == 1) leftFisted = true;   // (the shield fist leaves a hand that is posed)
    }
    // [shield] forearm: the shield strapped to the left forearm (StrapShield) - after everything that places the arms and the weapons,
    // before the shield's place is told to shield_smite (its capsule follows the shield as drawn).
    if (in.shieldForearm && in.mode == 2 && (in.handsValid & 2u)) {
        const bool strapped = StrapShield(h, in, oldW, newW, twistRoll[1]);
        g_shieldStrapped.store(strapped ? 1 : -1);
    } else {
        g_shieldStrapped.store(0);
    }
    // [hands] shield_fist: the left hand with a shield on it follows the controller's real grip too - the fingers only. Fist() rebuilds
    // the finger chains below the wrist (not the weapon attach bone the shield hangs on) after the arm is placed, so the shield and the
    // arm stay as the game draws them (vr/shield_fist.h). The weight model's inputs are not touched: that hand is told to it as busy.
    if (in.shieldFist) {
        shieldfist::Want want;
        want.on = true;
        want.f4 = viewgate::F4();   // (a view change shows in the very next pose)
        want.shield = true;         // vrcam set shieldFist for a shield in the left slot (gamestate::LeftShield())
        want.arms = in.mode == 2;
        want.tracked = (in.handsValid & 2u) != 0;
        want.rawGrip = in.shieldGrip;
        want.layoutGrip = in.grip[1];
        want.closedAlready = leftFisted;
        if (const float g = shieldfist::Grip(want); g >= 0.0f)
            Fist(h, 1, g,
                 in.fistCap[1] >= 0.0f ? std::min(in.fistClose[h.cls >= 0 ? h.cls : 3], in.fistCap[1]) : in.fistClose[h.cls >= 0 ? h.cls : 3],
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
    // and looking down sees the chest, not a neck.
    if (in.hideHead && h.head >= 0) {
        const V3 p = Pos(newW[h.neck >= 0 ? h.neck : h.head]);
        for (int i = 0; i < n; ++i) if (h.inNeck[i]) {
            Mat& w = newW[i];
            for (int r = 0; r < 3; ++r) for (int cc = 0; cc < 3; ++cc) w.m[r*4+cc] *= 0.001f;
            w.m[12] = p.x; w.m[13] = p.y; w.m[14] = p.z;
        }
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

bool OursNow(uintptr_t self, const Hero& hero) {
    uintptr_t world = 0, mats = 0;
    float head[16];
    const float standing = hero.head >= 0 && hero.head < (int)hero.bind.size() ? hero.bind[hero.head].m[13] : 0.0f;
    if (standing > 2.0f && Get(self + 0x20, &world) && Get(world + 4, &mats) &&
        SafeCopy(head, (const void*)(mats + (uintptr_t)hero.head * 64), sizeof head) &&
        !(std::isfinite(head[13]) && head[13] > 0.35f * standing)) {
        AcquireSRWLockExclusive(&g_bodiesLock); g_bodies.erase(self); ReleaseSRWLockExclusive(&g_bodiesLock);
        return false;
    }
    float L[3];
    ULONGLONG lookAt = 0, stillSince = 0;
    float x = 0.0f, z = 0.0f;
    const ULONGLONG now = GetTickCount64();
    if (!HeroPos(L, &lookAt, &stillSince) || now - lookAt > 1000 || !ecs::BodyPlace(self, &x, &z)) return true;
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
    if (mine == g_bodies.end()) return false;
    const auto ours = g_bodies.find(g_ours);
    if (ours == g_bodies.end()) g_ours = 0;
    if (self == g_ours) return true;
    const Body& b = mine->second;
    if (g_ours) {
        const Body& o = ours->second;
        const bool rebuilt = b.poses <= 3 && fabsf(b.x - o.x) < 1.0f && fabsf(b.z - o.z) < 1.0f;
        const bool mends = now - stillSince > 500 && b.dist + 1.0f < o.dist;
        if (!rebuilt && !mends) return false;
    } else {
        if (b.poses < 2) return false;   // every rig of a frame known
        for (const auto& [s, other] : g_bodies)
            if (s != self && now - other.at <= 100 && other.dist < b.dist) return false;
    }
    g_ours = self;
    return true;
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
            b.attach = newW[h.weapon[bowSide]];
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
            NoteBow("bow rig: " + model + " is '" + resolved.layout->name + "', " + std::to_string(count) + " bones; limbs " + names[resolved.bend[0]] + " / " +
                    names[resolved.bend[1]] + ", string ends " + names[resolved.tip[0]] + " / " + names[resolved.tip[1]] + ", nock " + names[resolved.nock] +
                    ", arrow " + names[resolved.arrow]);
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

// A held bow's skeleton that is NOT the hunter's 13-bone rig (the Hydra Bow, for one): said once per skeleton, so the log tells
// which layouts exist - the path and the bone count are what bow-rig-pose needs to learn them. Read only while the bow draw wants
// the string (it is the debugging case), and each skeleton instance once.
std::atomic<uintptr_t> g_bowProbeSeen[1024];
SRWLOCK g_bowProbeLock = SRWLOCK_INIT;
std::string g_bowProbeText;

void PoseBowRig(uintptr_t self) {
    const bool rigOn = g_bowRigOn.load(std::memory_order_relaxed), stringOn = g_bowStringOn.load(std::memory_order_relaxed), aimOn = g_bowAimOn.load(std::memory_order_relaxed);
    if (!rigOn && !stringOn && !aimOn) return;
    const ULONGLONG now = GetTickCount64();
    // the common case: the string is not drawn (and nobody wants to know where it rests or which way the bow points): nothing is looked at
    const bool holding = rigOn && bowrig::Holding(g_bowDrawnAt.load(), now);
    if (!holding && !stringOn && !aimOn) return;
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
    AcquireSRWLockExclusive(&g_bowRigLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_bowRigLock); } } unlock;
    const auto key = std::make_pair(self, skeleton);
    auto it = g_bowRigs.find(key);
    if (it == g_bowRigs.end() || it->second.bones != bones) {
        BowRigEntry e;
        e.bones = bones;
        LearnBow(self, bones, count, &e);
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
    } else {
        held.last = "the pose could not be written";
    }
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
    Hero hero; bool have = false;
    AcquireSRWLockShared(&g_heroLock);
    auto it = g_heroes.find(key);
    if (it != g_heroes.end()) { hero = it->second; have = true; }
    ReleaseSRWLockShared(&g_heroLock);
    if (!have) {
        Hero h;
        if (!Learn(self, &h)) h.count = 0;
        AcquireSRWLockExclusive(&g_heroLock);
        if (g_heroes.size() > 4096) g_heroes.clear();
        g_heroes[key] = h;
        ReleaseSRWLockExclusive(&g_heroLock);
        hero = h;
    }
    if (hero.count <= 0 || !OursNow(self, hero)) return;   // another player, our corpse
    NoteHero(self);
    if (!active) return;
    // The facing of this very frame, not of the last 10 ms tick - the hero only.
    if (in.haveYaw) if (const FreshFn f = g_fresh.load()) {
        float yaw = 0.0f, eye[3] = {}; bool eyeOk = false;
        if (f(&yaw, eye, &eyeOk)) {
            in.yawInModel = yaw;
            if (eyeOk) { memcpy(in.eyeModel, eye, sizeof eye); in.haveEye = true; }
        }
    }
    if (in.mode == 2) if (const FreshHandsFn f = g_freshHands.load()) f(&in);
    // [weight]: the weapon stepped (once per head sample) and the hands on it moved to where it has them
    weight::Out wo;
    weight::Apply(&in, &wo);
    PoseBody(self, hero, in, wo);
}

}  // namespace

// From vrcam, every 10 ms.
int LockState() { return g_lockState.load(); }

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
    g_bowStringOn.store(in.bowString && in.mode == 2);
#endif
}

using ComputeFn = void (*)(uintptr_t self, float a, float b, bool c);
ComputeFn g_orig = nullptr;

void __cdecl Hook(uintptr_t self, float a, float b, bool c) {
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
#if D2RVR_FIRST_PERSON
    PoseBowRig(self);   // [bow] rig_pose: the hero's bow, after his pose has told it where the hands are
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
void TrackHero(bool on) { g_trackHero.store(on); }
uint32_t HeroGen() { return g_heroGen.load(); }

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
        if (self == failSelf && now - failAt < 500) { if (why) *why = ecs::g_why; return false; }   // one pass per half second
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
void SetFreshHands(FreshHandsFn fn) { g_freshHands.store(fn); }

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
