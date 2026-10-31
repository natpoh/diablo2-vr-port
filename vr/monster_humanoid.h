// monster_humanoid.h - [weight] monster_ragdoll: is a monster's skeleton a humanoid's? (docs/notes/monster_ragdoll.md). The author's rule (2026-10-22):
// «механику скелета ставим только на гуманойдов» - the doll (push, stagger, weight on the skeleton) goes on humanoids only: zombies, fallen, skeletons,
// goatmen. Animals, insects, blobs, flyers, worms keep their bodies as they are, without the doll.
// Pure (cleanroom/tests/monster_humanoid_test.cpp). Decided from the model itself - its skeleton's bone names and hierarchy and its pose, no list of monsters.
//
// 1. The names (Judge, once a model): a head, a trunk (spine / pelvis / hips / chest / torso), an arm on each side hanging from the trunk, a leg on each side.
//    A side is a name's own word: L / R / Left / Right ("L_UpperArm", "shoulder_R_JNT", "Bip01 L Thigh", "LeftArm"). An arm: a bone with arm / forearm /
//    shoulder / clavicle / elbow / hand / wrist in its words; a leg: leg / thigh / calf / knee / shin / foot / ankle. A limb is a CHAIN of such bones of one
//    side - two or more, each the parent of the next; a lone one (a twist or helper bone hung off the pelvis, a strap) is no limb. At most two limbs of a kind
//    a side (a spider's three or four legs a side are too many). Names that tell nothing (no head, no sided limb) do not settle it: not a humanoid.
//    (After the first headset test, 2026-10-22: 'zombie1' was refused for "more than two legs" when every bone with "leg" in it began a limb.)
// 2. The pose (Upright, every read until it is seen): a four-legged beast on a two-legged rig (its forelegs named arms - 'quillrat1' passed the names and
//    was taken for a humanoid) must also be seen standing up - in one pose, five reads running: the head above the hips by half the hips' height over the
//    feet or more, the trunk more upright than flat (the head less far from the hips across the ground than above them), and the hands off the ground by a
//    quarter of the hips' height (a beast's forepaws are on it).
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "d2r_vr_fall.h"   // [weight] knock_down: the humanoid's joints (MapJoints)
#include "monster_rig.h"

namespace monsterhumanoid {

constexpr int kMaxWords = 12, kWordLen = 16;
constexpr int kMaxBones = bonecap::kCeiling;   // the arrays; a skeleton longer than bonecap::Max() ([weight] skeleton_bones_max) is not judged
constexpr int kMaxPerSide = 2;      // limbs of a kind on a side
constexpr int kUprightRun = 5;      // reads running that must be upright

// A bone's name as lower-case words: split at anything not a letter (digits too) and where the case turns ("LUpperArm" -> l upper arm,
// "R_upr_arm_bind_jnt" -> r upr arm bind jnt).
inline int Words(const char* name, char (*w)[kWordLen], int max) {
    int n = 0, len = 0;
    const auto flush = [&]() {
        if (len > 0 && n < max) { w[n][len < kWordLen ? len : kWordLen - 1] = 0; ++n; }
        len = 0;
    };
    for (int i = 0; name && name[i]; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!std::isalpha(c)) { flush(); continue; }
        if (len > 0 && std::isupper(c)) {
            const unsigned char prev = static_cast<unsigned char>(name[i - 1]), next = static_cast<unsigned char>(name[i + 1]);
            if (std::islower(prev) || (std::isupper(prev) && std::islower(next))) flush();
        }
        if (n >= max) break;
        if (len < kWordLen - 1) w[n][len] = static_cast<char>(std::tolower(c));
        ++len;
    }
    flush();
    return n;
}

inline bool Is(const char* w, const char* const* list) {
    for (int i = 0; list[i]; ++i)
        if (std::strcmp(w, list[i]) == 0) return true;
    return false;
}

enum Part : int8_t { kNone, kHead, kTrunk, kArm, kLeg };
struct Bone {
    Part part = kNone;
    int side = 0;   // -1 left, +1 right, 0 none
};

inline Bone Read(const char* name) {
    static const char* const kHeads[] = {"head", nullptr};
    static const char* const kTrunks[] = {"spine", "pelvis", "hips", "hip", "chest", "torso", "abdomen", nullptr};
    static const char* const kArms[] = {"arm", "forearm", "upperarm", "shoulder", "clavicle", "elbow", "hand", "wrist", nullptr};
    static const char* const kLegs[] = {"leg", "thigh", "calf", "knee", "shin", "foot", "ankle", "upperleg", "lowerleg", nullptr};
    char w[kMaxWords][kWordLen];
    const int n = Words(name, w, kMaxWords);
    Bone b;
    for (int i = 0; i < n; ++i) {
        if (!std::strcmp(w[i], "l") || !std::strcmp(w[i], "left")) b.side = -1;
        else if (!std::strcmp(w[i], "r") || !std::strcmp(w[i], "right")) b.side = 1;
    }
    for (int i = 0; i < n && b.part == kNone; ++i) {   // the first word that names a part decides ("hand" before "finger"; "head" of "head_M_JNT")
        if (Is(w[i], kArms)) b.part = kArm;
        else if (Is(w[i], kLegs)) b.part = kLeg;
        else if (Is(w[i], kHeads)) b.part = kHead;
        else if (Is(w[i], kTrunks)) b.part = kTrunk;
    }
    if ((b.part == kArm || b.part == kLeg) && b.side == 0) b.part = kNone;   // a limb of no side is no evidence either way
    return b;
}

// [weight] monster_bodies: does the bone's name say it is the body (monster_rig.h BodyBones keeps it, and every bone up to the root) - a head, a neck, a
// jaw, a trunk word, a tail, a wing, a limb word of any side? The author's screenshots 2026-10-24: a zombie without its head ("head_bind_jnt",
// "neck_bind_jnt" left out), a quill rat without its tail. A spike, a rope, a strip of cloth, a prop's bone say none of it.
inline bool BodyWord(const char* name) {
    static const char* const kWords[] = {"head", "neck", "jaw", "skull", "spine", "pelvis", "hips", "hip", "chest", "torso", "abdomen", "belly", "stomach",
                                         "body", "root", "tail", "wing", "arm", "forearm", "upperarm", "shoulder", "clavicle", "elbow", "hand", "wrist",
                                         "leg", "thigh", "calf", "knee", "shin", "foot", "ankle", "upperleg", "lowerleg", nullptr};
    char w[kMaxWords][kWordLen];
    const int n = Words(name, w, kMaxWords);
    for (int i = 0; i < n; ++i)
        if (Is(w[i], kWords)) return true;
    return false;
}

// [weight] monster_ragdoll_all: is the bone, by its name, no limb of the body - a finger, a toe, an end point, a twist or helper bone, a weapon's or an
// effect's attach point, an eye, a jaw, a shield's bone, or "anim", the bone the game moves its monsters by, on the floor under the hips? Such a bone
// gets no body of its own from its segments (vr/bone_segments.h) and none from a mesh box when every monster is a doll. (bearstonem/d2r-pcvr's rule,
// feat/monster-limb-push e935df9, limb_rig.h NotLimb, MIT: "anim"'s segment up to the hips made a zombie a torso that is not there.)
inline bool NotLimbName(const char* name) {
    if (!name || !name[0]) return false;
    char low[64];
    size_t n = 0;
    for (; name[n] && n < sizeof low - 1; ++n) low[n] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[n])));
    low[n] = 0;
    if (std::strcmp(low, "anim") == 0) return true;
    static const char* const kParts[] = {"finger", "toe", "nub", "twist", "weapon", "attach", "helper", "dummy", "footstep", "effect", "fx_", "_fx", "eye", "jaw",
                                         "shield", nullptr};
    for (int i = 0; kParts[i]; ++i)
        if (std::strstr(low, kParts[i])) return true;
    return false;
}

// A limb: the first bone of a chain, how many bones it has.
struct Limb {
    int16_t root = -1, bones = 0;
};

struct Verdict {
    bool humanoid = false;
    bool settled = false;    // the names said enough to decide (false: no head and no sided limb at all)
    int armsL = 0, armsR = 0, legsL = 0, legsR = 0;
    bool head = false, trunk = false, armsOnTrunk = false;
    const char* why = "";
    int headBone = -1;
    // The limbs found (for the pose and the log): [0] left, [1] right.
    Limb arms[2][kMaxPerSide], legs[2][kMaxPerSide];
    int bones = 0;
    int16_t limbOf[kMaxBones] = {};   // each bone: 1 + the index of its limb's root, or 0 (no limb's)
    int16_t parent[kMaxBones] = {};   // each bone's parent (-1: a root) - [weight] monster_ragdoll_pose turns a limb with its children
    uint8_t bodyWord[kMaxBones] = {}; // each bone: 1 its name says it is the body (BodyWord) - [weight] monster_bodies keeps it
    uint8_t notLimb[kMaxBones] = {};  // each bone: 1 its name says it is no limb (NotLimbName) - [weight] monster_ragdoll_all leaves it out
    char said[240] = {};              // the limbs by their first bones' names (the log)
    // [weight] knock_down: the bone whose origin is each joint of the knock-down's humanoid (shared/d2r_vr_fall.h order; -1: none - the mod puts it by its
    // neighbours), and whether every joint it cannot do without was found (MapJoints)
    int16_t joint[D2RF_JOINTS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    bool joints = false;
    char jointsSaid[160] = {};        // the joints by bone index, or what was missing (the log)
};

// [weight] knock_down: a limb bone's place in its chain, by its name's words. Mixamo's LeftArm / LeftForeArm / LeftHand, a Biped's L UpperArm / L Forearm,
// "R_upr_arm_bind_jnt", "LeftUpLeg" / "LeftLeg" / "LeftFoot" / "LeftToeBase", "Bip01 L Calf". A word that tells nothing ("leg_01"): none - the chain's order
// decides (MapJoints).
enum Role : int8_t { kRoleNone, kRoleClavicle, kRoleShoulderWord, kRoleUpper, kRoleFore, kRoleHand, kRoleHip, kRoleThigh, kRoleKnee, kRoleAnkle, kRoleToe };
inline Role RoleOf(const char* name, bool arm) {
    char w[kMaxWords][kWordLen];
    const int n = Words(name, w, kMaxWords);
    const auto has = [&](const char* const* list) {
        for (int i = 0; i < n; ++i)
            if (Is(w[i], list)) return true;
        return false;
    };
    if (arm) {
        static const char* const kHand[] = {"hand", "wrist", "palm", nullptr};
        static const char* const kFore[] = {"forearm", "fore", "lowerarm", "lower", "lwr", "elbow", "lo", nullptr};
        static const char* const kClav[] = {"clavicle", "clav", "collar", "collarbone", nullptr};
        static const char* const kUpper[] = {"upperarm", "upper", "upr", "up", nullptr};
        static const char* const kShoulder[] = {"shoulder", nullptr};
        static const char* const kArm[] = {"arm", nullptr};
        if (has(kHand)) return kRoleHand;
        if (has(kFore)) return kRoleFore;
        if (has(kClav)) return kRoleClavicle;
        if (has(kUpper)) return kRoleUpper;
        if (has(kShoulder)) return kRoleShoulderWord;
        if (has(kArm)) return kRoleUpper;
        return kRoleNone;
    }
    static const char* const kToe[] = {"toe", "toes", "ball", nullptr};
    static const char* const kAnkle[] = {"foot", "ankle", nullptr};
    static const char* const kKnee[] = {"calf", "shin", "knee", "lowerleg", "lower", "lwr", "lo", nullptr};
    static const char* const kThigh[] = {"thigh", "upperleg", "upper", "upr", "up", nullptr};
    static const char* const kHip[] = {"hip", "hips", "pelvis", nullptr};
    if (has(kToe)) return kRoleToe;
    if (has(kAnkle)) return kRoleAnkle;
    if (has(kKnee)) return kRoleKnee;
    if (has(kThigh)) return kRoleThigh;
    if (has(kHip)) return kRoleHip;
    return kRoleNone;
}

// [weight] knock_down: the humanoid's joints on its bones (Verdict::joint), from its limbs (the first of each side), their bones' names and the hierarchy.
// Each limb is walked down from its first bone (at each bone the child of the same limb with the most of it below); in an arm the shoulder is the first bone
// named an upper arm (else one named a shoulder; else the first that is no clavicle), the elbow the next named a forearm (else the next), the wrist the next
// named a hand (else the next); in a leg the hip the first named a thigh (else its first bone), then the knee, the ankle, the toe alike. The pelvis:
// the nearest bone above both legs; the chest: the nearest above both arms; the neck: the bone under the chest on the way to the head (none: the head's own
// bone is the neck joint and the head's top is put by the mod). Not found (`joints` false): no knock-down for the model.
inline void MapJoints(const char* const* names, Verdict& v) {
    const int n = v.bones;
    for (auto& j : v.joint) j = -1;
    v.joints = false;
    const auto chainOf = [&](int root, int16_t* out, int cap) {
        int k = 0;
        const int limb = v.limbOf[root];
        for (int b = root; b >= 0 && k < cap;) {
            out[k++] = static_cast<int16_t>(b);
            int best = -1, bestBelow = -1;
            for (int c = b + 1; c < n; ++c) {
                if (v.parent[c] != b || v.limbOf[c] != limb) continue;
                int below = 0;   // the limb's bones under c (parents come first)
                static thread_local uint8_t in[kMaxBones];
                std::memset(in, 0, static_cast<size_t>(n));
                in[c] = 1;
                for (int d = c + 1; d < n; ++d)
                    if (v.parent[d] >= 0 && in[v.parent[d]] && v.limbOf[d] == limb) { in[d] = 1; ++below; }
                if (below > bestBelow) { bestBelow = below; best = c; }
            }
            b = best;
        }
        return k;
    };
    const auto firstAfter = [&](int k, const Role* r, int from, Role want) {
        for (int i = from; i < k; ++i)
            if (r[i] == want) return i;
        return from < k ? from : -1;
    };
    const auto childNamed = [&](int b, Role want) {   // a child of b named as `want` (Mixamo's LeftToeBase: no limb's word, so no bone of the limb)
        for (int c = b + 1; c < n && b >= 0; ++c)
            if (v.parent[c] == b && names && names[c] && RoleOf(names[c], false) == want) return c;
        return -1;
    };
    int legRoot[2] = {-1, -1}, armRoot[2] = {-1, -1};
    for (int s = 0; s < 2; ++s) {
        const int shoulderJ = s ? D2RF_R_SHOULDER : D2RF_L_SHOULDER, hipJ = s ? D2RF_R_HIP : D2RF_L_HIP;
        if ((s ? v.armsR : v.armsL) > 0) {
            int16_t c[24];
            Role r[24];
            const int k = chainOf(armRoot[s] = v.arms[s][0].root, c, 24);
            for (int i = 0; i < k; ++i) r[i] = names && names[c[i]] ? RoleOf(names[c[i]], true) : kRoleNone;
            int sh = -1;
            for (int i = 0; i < k && sh < 0; ++i) if (r[i] == kRoleUpper) sh = i;
            for (int i = 0; i < k && sh < 0; ++i) if (r[i] == kRoleShoulderWord) sh = i;
            if (sh < 0) sh = k > 0 && r[0] == kRoleClavicle ? 1 : 0;
            const int el = sh + 1 < k ? firstAfter(k, r, sh + 1, kRoleFore) : -1;
            const int wr = el >= 0 && el + 1 < k ? firstAfter(k, r, el + 1, kRoleHand) : -1;
            if (sh < k) v.joint[shoulderJ] = c[sh];
            if (el >= 0) v.joint[shoulderJ + 1] = c[el];
            if (wr >= 0) v.joint[shoulderJ + 2] = c[wr];
        }
        if ((s ? v.legsR : v.legsL) > 0) {
            int16_t c[25];
            Role r[25];
            int k = chainOf(legRoot[s] = v.legs[s][0].root, c + 1, 24);
            for (int i = 1; i <= k; ++i) r[i] = names && names[c[i]] ? RoleOf(names[c[i]], false) : kRoleNone;
            // the assassin's "hip_L_JNT" is her thigh's bone, but "hip" is a trunk's word: her leg's chain begins at the knee - the bone above it is the hip
            const int above = v.parent[c[1]];
            const bool startsAtKnee = r[1] == kRoleKnee && above >= 0 && names && names[above] && RoleOf(names[above], false) == kRoleHip && Read(names[above]).side != 0;
            int first = 1;
            if (startsAtKnee) { c[0] = static_cast<int16_t>(above); r[0] = kRoleThigh; first = 0; legRoot[s] = above; }
            k += 1;
            int hp = -1;
            for (int i = first; i < k && hp < 0; ++i) if (r[i] == kRoleThigh) hp = i;
            if (hp < 0) hp = first;   // (no bone named a thigh: the chain's first - "leg_01")
            const int kn = hp + 1 < k ? firstAfter(k, r, hp + 1, kRoleKnee) : -1;
            const int an = kn >= 0 && kn + 1 < k ? firstAfter(k, r, kn + 1, kRoleAnkle) : -1;
            int to = an >= 0 && an + 1 < k ? firstAfter(k, r, an + 1, kRoleToe) : -1;
            if (hp < k) v.joint[hipJ] = c[hp];
            if (kn >= 0) v.joint[hipJ + 1] = c[kn];
            if (an >= 0) v.joint[hipJ + 2] = c[an];
            if (to >= 0) v.joint[hipJ + 3] = c[to];
            else if (an >= 0 && (to = childNamed(c[an], kRoleToe)) >= 0) v.joint[hipJ + 3] = static_cast<int16_t>(to);
        }
    }
    const auto commonAbove = [&](int a, int b) {   // the nearest bone above both (not a nor b themselves)
        if (a < 0 || b < 0) return -1;
        static thread_local uint8_t up[kMaxBones];
        std::memset(up, 0, static_cast<size_t>(n));
        for (int p = v.parent[a], g = 0; p >= 0 && g < n; p = v.parent[p], ++g) up[p] = 1;
        for (int p = v.parent[b], g = 0; p >= 0 && g < n; p = v.parent[p], ++g)
            if (up[p]) return p;
        return -1;
    };
    v.joint[D2RF_PELVIS] = static_cast<int16_t>(commonAbove(legRoot[0], legRoot[1]));
    v.joint[D2RF_CHEST] = static_cast<int16_t>(commonAbove(armRoot[0], armRoot[1]));
    if (v.headBone >= 0 && v.joint[D2RF_CHEST] >= 0) {
        int below = v.headBone;   // up from the head to the bone just under the chest
        for (int g = 0; g < n && v.parent[below] >= 0 && v.parent[below] != v.joint[D2RF_CHEST]; ++g) below = v.parent[below];
        if (v.parent[below] == v.joint[D2RF_CHEST] && below != v.headBone) {
            v.joint[D2RF_NECK] = static_cast<int16_t>(below);
            v.joint[D2RF_HEAD] = static_cast<int16_t>(v.headBone);
        } else {
            v.joint[D2RF_NECK] = static_cast<int16_t>(v.headBone);   // no neck of its own: the head's bone turns at the neck
        }
    }
    static const int kNeeded[] = {D2RF_PELVIS, D2RF_CHEST, D2RF_NECK, D2RF_L_SHOULDER, D2RF_L_ELBOW, D2RF_R_SHOULDER, D2RF_R_ELBOW,
                                  D2RF_L_HIP, D2RF_L_KNEE, D2RF_R_HIP, D2RF_R_KNEE};   // (an ankle no bone names: put by the mod, knockpose::JointsOf)
    static const char* const kJointName[D2RF_JOINTS] = {"pelvis", "chest", "neck", "head", "L shoulder", "L elbow", "L wrist", "R shoulder", "R elbow", "R wrist",
                                                        "L hip", "L knee", "L ankle", "L toe", "R hip", "R knee", "R ankle", "R toe"};
    const char* missing = nullptr;
    for (int j : kNeeded)
        if (v.joint[j] < 0 && !missing) missing = kJointName[j];
    if (!missing && v.joint[D2RF_PELVIS] == v.joint[D2RF_L_HIP]) missing = "the pelvis apart from the hips";
    v.joints = !missing;
    if (missing) {
        std::snprintf(v.jointsSaid, sizeof v.jointsSaid, "no %s", missing);
    } else {
        int at = 0;
        for (int j = 0; j < D2RF_JOINTS && at < (int)sizeof v.jointsSaid - 8; ++j)
            at += std::snprintf(v.jointsSaid + at, sizeof v.jointsSaid - at, "%s%d", j ? " " : "", (int)v.joint[j]);
    }
}

// The skeleton: n bones, names[i] (null or empty: unknown), parent[i] (-1: a root). Arms must hang from the trunk: a trunk bone among an arm root's
// ancestors.
inline Verdict Judge(const char* const* names, const int* parent, int n) {
    Verdict v;
    if (n <= 0 || n > bonecap::Max()) { v.why = n > 0 ? "too many bones to judge ([weight] skeleton_bones_max)" : "no skeleton"; return v; }
    v.bones = n;
    for (int i = 0; i < n; ++i) v.parent[i] = static_cast<int16_t>(parent[i] >= 0 && parent[i] < n && parent[i] != i ? parent[i] : -1);
    Bone bone[kMaxBones];
    for (int i = 0; i < n; ++i) {
        bone[i] = names[i] ? Read(names[i]) : Bone{};
        v.bodyWord[i] = names[i] && BodyWord(names[i]) ? 1 : 0;
        v.notLimb[i] = NotLimbName(names[i]) ? 1 : 0;
    }
    const auto parentOf = [&](int i) { const int p = parent[i]; return p >= 0 && p < n && p != i ? p : -1; };
    // each limb bone's root: up while the parent is a bone of the same part and side; how many bones each root has
    int root[kMaxBones], count[kMaxBones] = {};
    for (int i = 0; i < n; ++i) {
        root[i] = -1;
        if (bone[i].part != kArm && bone[i].part != kLeg) continue;
        int r = i;
        for (int guard = 0; guard < n; ++guard) {
            const int p = parentOf(r);
            if (p < 0 || bone[p].part != bone[i].part || bone[p].side != bone[i].side) break;
            r = p;
        }
        root[i] = r;
        ++count[r];
    }
    v.armsOnTrunk = true;
    int sidedLimbBones = 0;
    for (int i = 0; i < n; ++i) {
        const Bone& b = bone[i];
        if (b.part == kHead && !v.head) { v.head = true; v.headBone = i; }
        if (b.part == kTrunk) v.trunk = true;
        if (b.part == kArm || b.part == kLeg) ++sidedLimbBones;
        if (root[i] != i || count[i] < 2) continue;   // inside a chain, or a lone bone: no limb of its own
        const int s = b.side < 0 ? 0 : 1;
        int& c = b.part == kArm ? (s == 0 ? v.armsL : v.armsR) : (s == 0 ? v.legsL : v.legsR);
        if (c < kMaxPerSide) {
            Limb& l = b.part == kArm ? v.arms[s][c] : v.legs[s][c];
            l.root = static_cast<int16_t>(i);
            l.bones = static_cast<int16_t>(count[i]);
        }
        ++c;
        if (b.part == kArm) {
            bool onTrunk = false;
            for (int a = parentOf(i), guard = 0; a >= 0 && guard < n; a = parentOf(a), ++guard)
                if (bone[a].part == kTrunk) { onTrunk = true; break; }
            if (!onTrunk) v.armsOnTrunk = false;
        }
    }
    for (int i = 0; i < n; ++i) v.limbOf[i] = static_cast<int16_t>(root[i] >= 0 && count[root[i]] >= 2 ? root[i] + 1 : 0);
    {   // the log: "arms L shoulder_L_JNT(4) R ...; legs L ..."
        int at = 0;
        const auto add = [&](const char* s) { at += std::snprintf(v.said + at, at < (int)sizeof v.said ? sizeof v.said - at : 0, "%s", s); };
        const auto limbs = [&](const char* kind, const Limb (*l)[kMaxPerSide], int nl, int nr) {
            add(kind);
            for (int s = 0; s < 2; ++s) {
                add(s ? " R" : " L");
                const int c = s ? nr : nl;
                if (!c) add(" -");
                for (int k = 0; k < c && k < kMaxPerSide; ++k) {
                    char one[64];
                    std::snprintf(one, sizeof one, " %s(%d)", names[l[s][k].root] ? names[l[s][k].root] : "?", (int)l[s][k].bones);
                    add(one);
                }
                if (c > kMaxPerSide) add(" ...");
            }
        };
        limbs("arms", v.arms, v.armsL, v.armsR);
        add("; ");
        limbs("legs", v.legs, v.legsL, v.legsR);
        if (at >= (int)sizeof v.said) v.said[sizeof v.said - 1] = 0;
    }
    v.settled = v.head || sidedLimbBones > 0;
    const int legs = v.legsL + v.legsR, arms = v.armsL + v.armsR;
    if (!v.settled) v.why = "its bone names tell nothing (no head, no limb with a side)";
    else if (!v.head) v.why = "no head";
    else if (!v.trunk) v.why = "no trunk (spine, pelvis, hips, chest)";
    else if (v.legsL < 1 || v.legsR < 1) v.why = legs == 0 ? "no legs" : "not a leg on each side";
    else if (v.legsL > kMaxPerSide || v.legsR > kMaxPerSide) v.why = "more than two legs a side";
    else if (v.armsL < 1 || v.armsR < 1) v.why = arms == 0 ? "no arms" : "not an arm on each side";
    else if (v.armsL > kMaxPerSide || v.armsR > kMaxPerSide) v.why = "more than two arms a side";
    else if (!v.armsOnTrunk) v.why = "its arms do not hang from its trunk";
    else { v.humanoid = true; v.why = "a head, a trunk, an arm on each side on it, a leg on each side"; }
    if (v.humanoid) MapJoints(names, v);   // [weight] knock_down
    return v;
}

// Is the skeleton standing up in this pose? pos: each bone's place in the world (metres or the game's units - only ratios count; up is +y), n bones.
// Only for a verdict the names passed. why (may be null): what failed.
inline bool Upright(const Verdict& v, const float (*pos)[3], int n, const char** why = nullptr) {
    const auto fail = [&](const char* w) { if (why) *why = w; return false; };
    if (!v.humanoid || v.headBone < 0 || v.headBone >= n || n != v.bones) return fail("not judged");
    // each side's leg that reaches lowest (its foot), the hips where those legs begin, the feet the lowest of their bones
    float feet = 1e30f, hips[3] = {}, hipsN = 0.0f;
    for (int s = 0; s < 2; ++s) {
        const int nl = s ? v.legsR : v.legsL;
        int best = -1;
        float bestLow = 1e30f;
        for (int k = 0; k < nl && k < kMaxPerSide; ++k) {
            const int r = v.legs[s][k].root;
            float low = 1e30f;
            for (int i = 0; i < n; ++i)
                if (v.limbOf[i] == r + 1 && pos[i][1] < low) low = pos[i][1];
            if (low < bestLow) { bestLow = low; best = r; }
        }
        if (best < 0) return fail("no leg");
        if (bestLow < feet) feet = bestLow;
        for (int k = 0; k < 3; ++k) hips[k] += pos[best][k];
        hipsN += 1.0f;
    }
    for (int k = 0; k < 3; ++k) hips[k] /= hipsN;
    const float legs = hips[1] - feet;
    if (!(legs > 0.0f)) return fail("its hips are not above its feet");
    const float* head = pos[v.headBone];
    const float over = head[1] - hips[1];
    if (!(over > 0.5f * legs)) return fail("its head is not well above its hips");
    const float dx = head[0] - hips[0], dz = head[2] - hips[2];
    if (!(dx * dx + dz * dz < over * over)) return fail("its trunk is flatter than upright");
    for (int s = 0; s < 2; ++s) {   // each side's arms: the lowest point of the highest-hanging one off the ground
        const int na = s ? v.armsR : v.armsL;
        float handsLow = -1e30f;
        for (int k = 0; k < na && k < kMaxPerSide; ++k) {
            const int r = v.arms[s][k].root;
            float low = 1e30f;
            for (int i = 0; i < n; ++i)
                if (v.limbOf[i] == r + 1 && pos[i][1] < low) low = pos[i][1];
            if (low > handsLow) handsLow = low;
        }
        if (!(handsLow - feet > 0.25f * legs)) return fail("its hands are on the ground");
    }
    if (why) *why = "standing";
    return true;
}

// [weight] own_body: the hero's bones his held weapon may not pass through - his trunk and legs; with head, his head too (Serg, 0.192.36: "чтобы оружие не
// проходило сквозь собственное тело"); with arm[side] (0 left, 1 right: a FREE hand - not on the weapon, no shield, no second weapon), that arm down to its
// wrist (the hand itself and all it carries left out: the joints' wrist, MapJoints; not found - the whole arm left out). Left out always: the hands that hold
// the weapon and all they carry, and the bones that are no limbs (NotLimbName: weapons' attach points, fingers, helpers). keep[i] 1: bone i is his body. False
// when the verdict found no arms or no head to leave out.
inline bool OwnBodyBones(const Verdict& v, uint8_t* keep, bool head = false, const bool* arm = nullptr) {
    const int n = v.bones;
    if (n <= 0 || n > kMaxBones || v.headBone < 0 || v.armsL + v.armsR == 0) return false;
    static thread_local uint8_t out[kMaxBones];
    for (int i = 0; i < n; ++i) out[i] = 0;
    const auto drop = [&](int root) { if (root >= 0 && root < n) out[root] = 1; };
    if (!head) drop(v.headBone);
    for (int s = 0; s < 2; ++s) {
        const int wrist = v.joints ? v.joint[s ? D2RF_R_WRIST : D2RF_L_WRIST] : -1;   // (the verdict's [0] is the left side, as the joints' L)
        if (arm && arm[s] && wrist >= 0) {
            drop(wrist);
            continue;
        }
        for (int k = 0; k < kMaxPerSide && k < (s ? v.armsR : v.armsL); ++k) drop(v.arms[s][k].root);
    }
    for (int i = 0; i < n; ++i) {   // (parents come first: a bone under one left out is left out)
        const int p = v.parent[i];
        if (p >= 0 && p < i && out[p]) out[i] = 1;
    }
    for (int i = 0; i < n; ++i) keep[i] = !out[i] && !v.notLimb[i] ? 1 : 0;
    return true;
}

// The verdict on a granny_skeleton in the game's memory (vr/monster_rig.h's layout: +0x08 the count, +0x0C the bones, each 0xA4: +0x00 its name,
// +0x08 its parent). read(address, out, bytes) as monster_rig's. A skeleton that does not read: not settled.
template <class Read>
Verdict JudgeSkeleton(Read&& read, uintptr_t skeleton) {
    using namespace monsterrig;
    int32_t count = 0;
    uintptr_t bones = 0;
    // (the arrays below hold monsterhumanoid::kMaxBones; a skeleton longer than that is not judged. They were monsterrig's 160 while the count was let up to
    // this 256: a model of 161 - 256 bones, met on leaving the Arcane Sanctuary, wrote past them and the game died in Read - 2026-10-23)
    if (skeleton < 0x10000 || !read(skeleton + kSkelCount, &count, sizeof count) || count < 1 || count > bonecap::Max() ||
        !read(skeleton + kSkelBones, &bones, sizeof bones) || bones < 0x10000) {
        Verdict v;
        v.why = count > bonecap::Max() ? "too many bones to judge ([weight] skeleton_bones_max)" : "its skeleton does not read";
        return v;
    }
    static thread_local char names[monsterhumanoid::kMaxBones][kNameLen];
    const char* named[monsterhumanoid::kMaxBones];
    int parent[monsterhumanoid::kMaxBones];
    for (int i = 0; i < count; ++i) {
        uintptr_t p = 0;
        int32_t up = -1;
        names[i][0] = 0;
        if (!read(bones + (uintptr_t)i * kBoneStride + kBoneName, &p, sizeof p) || !grannymesh::String(read, p, names[i])) names[i][0] = 0;
        if (!read(bones + (uintptr_t)i * kBoneStride + kBoneParent, &up, sizeof up)) up = -1;
        named[i] = names[i];
        parent[i] = up;
    }
    return Judge(named, parent, count);
}

}  // namespace monsterhumanoid
