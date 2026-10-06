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
#include <vector>

#include "skeletons.h"

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

float g_heroPos[3] = {};   // the world camera's look-at, from vrcam

// The hero's world placement is applied at draw time, not in the pose. Look for
// it near the SkeletonInstance: in it, in what it points at, and one more hop -
// any spot whose floats read as the hero's x and z, with the floats around it.
void FindHeroTransform(uintptr_t self) {
    fprintf(g_out, "   looking for the hero position %.2f %.2f %.2f near the instance\n", g_heroPos[0], g_heroPos[1], g_heroPos[2]);
    std::set<uintptr_t> scanned;
    int hits = 0;
    auto scan = [&](uintptr_t base, const char* how, uintptr_t via) {
        if (base < 0x10000 || !scanned.insert(base).second || hits > 40) return;
        float f[0x600 / 4];
        if (!SafeCopy(f, (const void*)base, sizeof f)) return;
        for (int i = 0; i + 2 < (int)(sizeof f / 4) && hits <= 40; ++i) {
            if (fabsf(f[i] - g_heroPos[0]) > 3.0f) continue;
            const int zi = fabsf(f[i + 1] - g_heroPos[2]) < 3.0f ? i + 1 : fabsf(f[i + 2] - g_heroPos[2]) < 3.0f ? i + 2 : -1;
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
    struct Finger { std::vector<int> bones; int firstBend = 0; bool thumb = false; };
    std::vector<Finger> fingers[2];
    bool bindOk = false;
    int cls = -1;   // kHeroClasses, from the model's folder
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
std::atomic<int> g_gunAxis{0};   // GunAxis
struct GunBoneOut { float o[3], ax[3][3]; ULONGLONG at; };
SRWLOCK g_gunBoneLock = SRWLOCK_INIT;
GunBoneOut g_gunBone{};
// [debug] the line the left hand takes the weapon by (staff, spear, sword), the same frame
struct GrabLineOut { float a[3], b[3]; ULONGLONG at; };
GrabLineOut g_grabLine{};
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
// Each storage type is told by its vtable, so nothing here depends on the type index order.
constexpr uint64_t RVA_ECS_REGISTRY = 0x2677170;
constexpr uint64_t RVA_VT_TRANSFORM_STORAGE = 0x1CE0958;   // storage<TransformComponent>, made in 0x2035B0
constexpr uint64_t RVA_VT_SKELETON_STORAGE = 0x1D4A000;    // storage<SkeletonComponent>, made in 0x66D4F0
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
uintptr_t g_xs = 0, g_ss = 0;               // the two storages, found by vtable
uintptr_t g_cachedSelf = 0;
uint32_t g_cachedEntity = 0;
ULONGLONG g_lastStorageLook = 0;
const char* g_why = "";                     // why the chain does not read (for vrcam's log)

bool IsStorage(uintptr_t st, uint64_t vtRva) {
    uintptr_t vt = 0;
    return st && Get(st, &vt) && vt == g_gameBase.load() + vtRva;
}

bool FindStorages() {
    const uintptr_t reg = g_gameBase.load() + RVA_ECS_REGISTRY;
    uintptr_t b = 0, e = 0;
    if (!Get(reg, &b) || !Get(reg + 8, &e) || e <= b || (e - b) % 0x28 || (e - b) / 0x28 > 4096) { g_why = "the registry's pools do not read"; return false; }
    const size_t n = (e - b) / 0x28;
    std::vector<uint8_t> pools(n * 0x28);
    if (!SafeCopy(pools.data(), (const void*)b, pools.size())) { g_why = "the registry's pools do not read"; return false; }
    uintptr_t xs = 0, ss = 0;
    for (size_t i = 0; i < n && !(xs && ss); ++i) {
        uintptr_t st = 0; memcpy(&st, pools.data() + i * 0x28 + 0x20, 8);
        if (!st) continue;
        if (!xs && IsStorage(st, RVA_VT_TRANSFORM_STORAGE)) xs = st;
        else if (!ss && IsStorage(st, RVA_VT_SKELETON_STORAGE)) ss = st;
    }
    g_xs = xs; g_ss = ss;
    if (!xs || !ss) { g_why = !xs ? "no TransformComponent storage in the registry" : "no SkeletonComponent storage in the registry"; return false; }
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
struct ForearmOut { float elbow[3], wrist[3], across[3]; ULONGLONG at; };

SRWLOCK g_forearmLock = SRWLOCK_INIT;
ForearmOut g_forearm[2] = {};

// ToModel's inverse: a model-space vector to the hand frame.
void FromModel(V3 o, float c, float s, float out[3]) {
    const float mx = o.x * c - o.z * s, mz = o.x * s + o.z * c;
    out[0] = -mx; out[1] = o.y; out[2] = -mz;
}

// inReach: the left hand is close enough to the staff to take it. A grip pressed
// far from it takes nothing until it is let go and pressed again near it.
float StaffEase(float grip, bool inReach) {
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&g_staffLock);
    const float dt = g_staff.last ? std::clamp((float)((double)(now.QuadPart - g_staff.last) / (double)f.QuadPart), 0.0f, kStaffEaseS) : kStaffEaseS;
    g_staff.last = now.QuadPart;
    const bool pressed = grip < 0.0f || grip > 0.6f ? true : grip < 0.4f ? false : g_staff.pressed;
    if (grip < 0.0f) g_staff.on = true;   // no grip from the bridge: always on the staff
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

void Fist(const Hero& h, int side, float grip, float closed, float open, const std::vector<Mat>& oldW, std::vector<Mat>& newW) {
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
            const float t = deg[n] * amount * 0.0174532925f;
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
}

#endif
void PoseBody(uintptr_t self, const Hero& h, const Input& in) {
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
    // The animation after the turn: what the arms re-derive their children from.
    const std::vector<Mat> baseW = newW;

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

    // The grip, taken once per weapon while not attacking (see Grip).
    Grip grip;
    AcquireSRWLockShared(&g_gripLock); grip = g_grip; ReleaseSRWLockShared(&g_gripLock);
    if ((grip.type != in.weaponType || !grip.ok) && !in.attacking && h.weapon[0] >= 0 && h.weapon[1] >= 0 && h.hand[0] >= 0 && h.hand[1] >= 0) {
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
            const float e = (in.handsValid & 2u) ? StaffEase(in.grip[1], inReach) : 1.0f;
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
            for (int i = 0; i < 3; ++i) { f.elbow[i] -= in.handOffset[i]; f.wrist[i] -= in.handOffset[i]; }
            f.at = GetTickCount64();
            AcquireSRWLockExclusive(&g_forearmLock); g_forearm[side] = f; ReleaseSRWLockExclusive(&g_forearmLock);
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
            newW[to] = newW[from];
            // ...and moving, the game lays a two-handed weapon along the line from the right attach bone
            // to the left one (both hands on the haft): with the two in one point the line had no length
            // and the axe was squashed flat along its Y (10:23 log: its matrix had Y 0, both bones Y 1).
            // So the left one goes along the right one's own Y (+-in.carryAxis), 40 cm out.
            {
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
            const bool flip = (det(oldW[to]) < 0.0f) != (det(newW[from]) < 0.0f);
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
    }

    // [hands] fist: free hands close and open with the controllers' grips.
    if (h.cls >= 0) g_heroClass.store(h.cls);
    if (in.fist && in.mode == 2)
        for (int side = 0; side < 2; ++side)
            if (in.handFree[side] && (in.handsValid & (1u << side)) && in.grip[side] >= 0.0f && !(side == 1 && leftOnStaff))   // on a shaft: the game's grip, not our fist (it wraps better, 2026-10-06)
                Fist(h, side, in.grip[side], in.fistClose[h.cls >= 0 ? h.cls : 3], in.fistOpen[h.cls >= 0 ? h.cls : 3], oldW, newW);

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
    if (hero.count <= 0) return;
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
    PoseBody(self, hero, in);
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
bool GunBone(float origin[3], float axes[3][3]) {
    AcquireSRWLockShared(&g_gunBoneLock); const GunBoneOut g = g_gunBone; ReleaseSRWLockShared(&g_gunBoneLock);
    if (!g.at || GetTickCount64() - g.at > 300) return false;
    memcpy(origin, g.o, sizeof g.o); memcpy(axes, g.ax, sizeof g.ax);
    return true;
}

void ResetGrip() { AcquireSRWLockExclusive(&g_gripLock); g_grip.ok = false; ReleaseSRWLockExclusive(&g_gripLock); }
#endif

#if D2RVR_FIRST_PERSON
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
}

using ComputeFn = void (*)(uintptr_t self, float a, float b, bool c);
ComputeFn g_orig = nullptr;

void __cdecl Hook(uintptr_t self, float a, float b, bool c) {
    g_orig(self, a, b, c);
    g_calls++;
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

bool HeroTransform(HeroXform* out, const char** why) {
    static const char* const kNoHero = "the hero has not been posed in the last 2 s";
    const uintptr_t self = g_heroSelf.load();
    if (!g_gameBase.load() || !self || GetTickCount64() - g_heroSeen.load() > 2000) { if (why) *why = kNoHero; return false; }
    AcquireSRWLockExclusive(&ecs::g_lock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&ecs::g_lock); } } unlock;
    if (!ecs::IsStorage(ecs::g_xs, RVA_VT_TRANSFORM_STORAGE) || !ecs::IsStorage(ecs::g_ss, RVA_VT_SKELETON_STORAGE)) {
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

void SetHeroPos(float x, float y, float z) { g_heroPos[0] = x; g_heroPos[1] = y; g_heroPos[2] = z; }

void** OrigSlot() { return (void**)&g_orig; }
void* Detour() { return (void*)&Hook; }

}  // namespace skel
