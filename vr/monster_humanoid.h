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

#include "monster_rig.h"

namespace monsterhumanoid {

constexpr int kMaxWords = 12, kWordLen = 16;
constexpr int kMaxBones = 256;      // a skeleton longer than this is not judged
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
};

// The skeleton: n bones, names[i] (null or empty: unknown), parent[i] (-1: a root). Arms must hang from the trunk: a trunk bone among an arm root's
// ancestors.
inline Verdict Judge(const char* const* names, const int* parent, int n) {
    Verdict v;
    if (n <= 0 || n > kMaxBones) { v.why = n > kMaxBones ? "too many bones to judge" : "no skeleton"; return v; }
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

// The verdict on a granny_skeleton in the game's memory (vr/monster_rig.h's layout: +0x08 the count, +0x0C the bones, each 0xA4: +0x00 its name,
// +0x08 its parent). read(address, out, bytes) as monster_rig's. A skeleton that does not read: not settled.
template <class Read>
Verdict JudgeSkeleton(Read&& read, uintptr_t skeleton) {
    using namespace monsterrig;
    int32_t count = 0;
    uintptr_t bones = 0;
    // (the arrays below hold monsterhumanoid::kMaxBones; a skeleton longer than that is not judged. They were monsterrig's 160 while the count was let up to
    // this 256: a model of 161 - 256 bones, met on leaving the Arcane Sanctuary, wrote past them and the game died in Read - 2026-10-23)
    if (skeleton < 0x10000 || !read(skeleton + kSkelCount, &count, sizeof count) || count < 1 || count > monsterhumanoid::kMaxBones ||
        !read(skeleton + kSkelBones, &bones, sizeof bones) || bones < 0x10000) {
        Verdict v;
        v.why = count > monsterhumanoid::kMaxBones ? "too many bones to judge" : "its skeleton does not read";
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
